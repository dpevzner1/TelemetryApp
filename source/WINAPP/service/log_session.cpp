#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <sstream>

#include "log_session.h"
#include "sensors/power.h"
#include "../shared/shm_layout.h"
#include "../shared/metric_ids.h"

// nlohmann/json is available in the service build via vcpkg
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace Service {

// ── Utilities ─────────────────────────────────────────────────────────────────

int64_t LogSessionStore::NowMs() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u{ ft.dwLowDateTime, ft.dwHighDateTime };
    // Windows epoch is Jan 1, 1601; Unix is Jan 1, 1970 → offset 116444736000000000 × 100ns
    return static_cast<int64_t>((u.QuadPart - 116444736000000000ULL) / 10000ULL);
}

std::string LogSessionStore::GenerateId() {
    uint8_t buf[4]{};
    BCryptGenRandom(nullptr, buf, sizeof(buf), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    char hex[16]{};
    snprintf(hex, sizeof(hex), "sess-%02x%02x%02x%02x",
             buf[0], buf[1], buf[2], buf[3]);
    return std::string(hex);
}

void LogSessionStore::EnsureDir(const std::string& path) {
    // Create directory tree (mkdir -p equivalent on Windows)
    std::string cur;
    for (char c : path) {
        cur += c;
        if (c == '\\' || c == '/') CreateDirectoryA(cur.c_str(), nullptr);
    }
    CreateDirectoryA(path.c_str(), nullptr);
}

// ── Singleton ─────────────────────────────────────────────────────────────────

LogSessionStore& LogSessionStore::Instance() {
    static LogSessionStore s_store;
    return s_store;
}

void LogSessionStore::Init(const std::string& data_dir) {
    std::lock_guard<std::mutex> lk(m_mu);
    m_sessions_dir = data_dir + "\\logs\\sessions";
    EnsureDir(m_sessions_dir);
    m_initialised = true;
}

// ── Start ─────────────────────────────────────────────────────────────────────

std::string LogSessionStore::Start(const std::string& label,
                                   const std::vector<uint32_t>& metric_ids,
                                   const std::string& log_dir,
                                   int64_t duration_ms,
                                   bool stop_when_watch_empty) {
    if (!m_initialised) return {};

    // Resolve target directory: caller-supplied (absolute) or default
    std::string target_dir;
    if (!log_dir.empty()) {
        // Require absolute path (Windows: must start with a drive letter or UNC)
        if (log_dir.size() < 3 ||
            !(isalpha(log_dir[0]) && log_dir[1] == ':') &&
            !(log_dir[0] == '\\' && log_dir[1] == '\\')) {
            return {};  // invalid path — caller should check for empty return
        }
        target_dir = log_dir;
    } else {
        target_dir = m_sessions_dir;
    }
    EnsureDir(target_dir);

    auto sess = std::make_unique<LogSession>();
    sess->id             = GenerateId();
    sess->label          = label.empty() ? sess->id : label;
    sess->started_at_ms  = NowMs();
    sess->metric_ids     = metric_ids;
    sess->status         = SessionStatus::Active;
    sess->log_dir        = target_dir;
    sess->log_path       = target_dir + "\\" + sess->id + ".jsonl";
    sess->meta_path      = target_dir + "\\" + sess->id + ".meta.json";
    sess->duration_ms    = duration_ms > 0 ? duration_ms : 0;
    sess->stop_when_watch_empty = stop_when_watch_empty;

    // Open log file for appending
    sess->file.open(sess->log_path, std::ios::app | std::ios::out);
    if (!sess->file.is_open()) return {};

    std::string id = sess->id;

    std::lock_guard<std::mutex> lk(m_mu);
    WriteMeta(*sess);
    m_sessions.push_back(std::move(sess));
    return id;
}

// ── Stop ──────────────────────────────────────────────────────────────────────

std::string LogSessionStore::Stop(const std::string& session_id) {
    std::lock_guard<std::mutex> lk(m_mu);
    LogSession* s = FindLocked(session_id);
    if (!s || s->status != SessionStatus::Active) return {};

    StopLocked(*s, NowMs());
    return s->log_path;
}

// ── Delete ────────────────────────────────────────────────────────────────────

bool LogSessionStore::Delete(const std::string& session_id) {
    std::lock_guard<std::mutex> lk(m_mu);
    auto it = std::find_if(m_sessions.begin(), m_sessions.end(),
        [&](const std::unique_ptr<LogSession>& s){ return s->id == session_id; });
    if (it == m_sessions.end()) return false;

    auto& s = *it;
    if (s->file.is_open()) s->file.close();
    DeleteFileA(s->log_path.c_str());
    DeleteFileA(s->meta_path.c_str());
    m_sessions.erase(it);
    return true;
}

// ── List / Find ───────────────────────────────────────────────────────────────

std::vector<LogSessionStore::SessionSummary> LogSessionStore::List() const {
    std::lock_guard<std::mutex> lk(m_mu);
    std::vector<SessionSummary> out;
    out.reserve(m_sessions.size());
    for (const auto& s : m_sessions) {
        out.push_back({s->id, s->label, s->log_path, s->log_dir, s->status,
                       s->started_at_ms, s->ended_at_ms,
                       s->duration_ms, s->stop_when_watch_empty,
                       s->rows_written, s->metric_ids});
    }
    return out;
}

bool LogSessionStore::FindSummary(const std::string& id,
                                   SessionSummary& out) const {
    std::lock_guard<std::mutex> lk(m_mu);
    for (const auto& s : m_sessions) {
        if (s->id == id) {
            out = {s->id, s->label, s->log_path, s->log_dir, s->status,
                   s->started_at_ms, s->ended_at_ms,
                   s->duration_ms, s->stop_when_watch_empty,
                   s->rows_written, s->metric_ids};
            return true;
        }
    }
    return false;
}

// ── Locked helpers ────────────────────────────────────────────────────────────

LogSession* LogSessionStore::FindLocked(const std::string& id) {
    for (auto& s : m_sessions)
        if (s->id == id) return s.get();
    return nullptr;
}

void LogSessionStore::StopLocked(LogSession& s, int64_t ended_at_ms) {
    s.ended_at_ms = ended_at_ms;
    s.status      = SessionStatus::Stopped;
    if (s.file.is_open()) { s.file.flush(); s.file.close(); }
    WriteMeta(s);
    AppendIndex(s);
}

void LogSessionStore::WriteMeta(LogSession& s) {
    json j;
    j["session_id"]    = s.id;
    j["label"]         = s.label;
    j["started_at_ms"] = s.started_at_ms;
    j["ended_at_ms"]   = s.ended_at_ms;
    j["status"]        = (s.status == SessionStatus::Active)   ? "active"
                       : (s.status == SessionStatus::Stopped)  ? "stopped"
                                                               : "failed";
    j["rows_written"]  = s.rows_written;
    j["log_dir"]       = s.log_dir;
    j["log_path"]      = s.log_path;
    j["duration_ms"]   = s.duration_ms;
    j["stop_when_watch_empty"] = s.stop_when_watch_empty;
    j["metric_ids"]    = s.metric_ids;

    std::ofstream f(s.meta_path, std::ios::trunc);
    if (f.is_open()) f << j.dump(2);
}

void LogSessionStore::AppendIndex(const LogSession& s) {
    std::string idx_path = m_sessions_dir + "\\index.jsonl";
    std::ofstream f(idx_path, std::ios::app);
    if (!f.is_open()) return;
    json j;
    j["session_id"]    = s.id;
    j["label"]         = s.label;
    j["started_at_ms"] = s.started_at_ms;
    j["ended_at_ms"]   = s.ended_at_ms;
    j["rows_written"]  = s.rows_written;
    j["log_path"]      = s.log_path;
    f << j.dump() << '\n';
}

static json PowerReading(double value,
                         const char* unit,
                         const char* source,
                         const char* quality,
                         const char* confidence,
                         const char* reason) {
    json j;
    j["value"] = value;
    j["unit"] = unit;
    j["source"] = source;
    j["quality"] = quality;
    j["confidence"] = confidence;
    j["reason"] = reason;
    return j;
}

static json PowerReading(const Sensors::ReadingQuality& r) {
    return PowerReading(r.value,
                        r.unit.c_str(),
                        r.source.c_str(),
                        r.quality.c_str(),
                        r.confidence.c_str(),
                        r.reason.c_str());
}

static bool ContainsNoCase(const std::string& s, const char* needle) {
    std::string a = s;
    std::string b = needle ? needle : "";
    std::transform(a.begin(), a.end(), a.begin(),
        [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    std::transform(b.begin(), b.end(), b.begin(),
        [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return !b.empty() && a.find(b) != std::string::npos;
}

static std::string GpuPowerSource(const std::string& name) {
    if (ContainsNoCase(name, "nvidia") || ContainsNoCase(name, "geforce") ||
        ContainsNoCase(name, "quadro") || ContainsNoCase(name, "rtx")) {
        return "NVML";
    }
    if (ContainsNoCase(name, "intel") || ContainsNoCase(name, "arc")) {
        return "Intel ControlLib";
    }
    if (ContainsNoCase(name, "amd") || ContainsNoCase(name, "radeon")) {
        return "AMD provider";
    }
    return "Vendor GPU API";
}

static json GpuPowerReading(const std::string& name, double watts) {
    std::string source = GpuPowerSource(name);
    if (watts > 0.0) {
        return PowerReading(watts, "W", source.c_str(), "measured", "high", "");
    }
    const char* reason = "GPU power provider did not report a positive watt reading";
    if (source == "AMD provider") {
        reason = "AMD GPU power is not populated by the current ADL collector";
    }
    return PowerReading(0.0, "W", source.c_str(), "unavailable", "none", reason);
}

static json BuildPowerRow(ShmBlock* shm) {
    json power;
    auto platform = Sensors::QueryPlatformPower();
    power["system"]["ac_power_state"] = PowerReading(platform.ac_power_state);
    power["system"]["battery_percent"] = PowerReading(platform.battery_percent);
    power["system"]["battery_rate_w"] = PowerReading(platform.battery_rate_w);
    power["system"]["platform_power_w"] = PowerReading(platform.platform_power_w);

    double cpu_pkg_w = shm ? shm->metrics[MetricId::CPU_PACKAGE_POWER_W].current : 0.0;
    power["cpu"]["package_power_w"] =
        cpu_pkg_w > 0.0
            ? PowerReading(cpu_pkg_w, "W", "CPU package provider", "measured", "medium", "")
            : PowerReading(0.0, "W", "CPU package provider", "unavailable", "none",
                           "CPU package power provider is not implemented or not supported");
    power["gpu"] = json::array();
    if (shm) {
        for (uint32_t gi = 0; gi < shm->hdr.active_gpu_count && gi < 4; ++gi) {
            std::string name = shm->hdr.gpu_name[gi];
            double watts = shm->metrics[gpu_metric(gi, GpuOff::POWER_W)].current;
            power["gpu"].push_back({
                {"index", gi},
                {"name", name},
                {"power_w", GpuPowerReading(name, watts)}
            });
        }
    }
    power["process_power"] =
        PowerReading(0.0, "W", "Allocation model", "unavailable", "none",
                     "Per-process electrical attribution is not implemented; current process telemetry supports future estimation");
    return power;
}

// ── Poll tick — write JSONL rows ──────────────────────────────────────────────

void LogSessionStore::OnPollTick(ShmBlock* shm) {
    if (!shm || !m_initialised) return;

    // Fast path: no lock if no active sessions (avoids mutex contention on every tick)
    // We read m_sessions under lock only to build a local active list.
    std::vector<LogSession*> active;
    {
        std::lock_guard<std::mutex> lk(m_mu);
        for (auto& s : m_sessions)
            if (s->status == SessionStatus::Active && s->file.is_open())
                active.push_back(s.get());
        if (active.empty()) return;
    }

    // Read from SHM without a lock — poll loop completed ShmEndWrite before calling us,
    // so the seqlock is even and values are stable for this tick.
    for (LogSession* s : active) {
        // Lock only to check the session is still active and to write to the file
        std::lock_guard<std::mutex> lk(m_mu);
        if (s->status != SessionStatus::Active || !s->file.is_open()) continue;
        try {
            WriteRow(*s, shm);
            int64_t now_ms = NowMs();
            if (shm->hdr.active_watch_count > 0) s->watch_seen = true;
            bool duration_done = s->duration_ms > 0 &&
                now_ms >= s->started_at_ms + s->duration_ms;
            bool watched_done = s->stop_when_watch_empty &&
                s->watch_seen && shm->hdr.active_watch_count == 0;
            if (duration_done || watched_done) {
                StopLocked(*s, now_ms);
            }
        } catch (...) {
            // If file write fails, mark session failed so we stop trying
            s->status = SessionStatus::Failed;
            if (s->file.is_open()) s->file.close();
        }
    }
}

void LogSessionStore::WriteRow(LogSession& s, ShmBlock* shm) {
    json row;
    row["ts_ms"]      = NowMs();
    row["session_id"] = s.id;
    row["label"]      = s.label;
    row["seq"]        = ++s.seq;

    if (s.metric_ids.empty()) {
        // ── Full snapshot row (human-readable named keys) ──────────────────────
        // CPU
        row["cpu_pct"]          = shm->metrics[MetricId::CPU_USAGE_TOTAL].current;
        row["cpu_freq_mhz"]     = shm->metrics[MetricId::CPU_FREQ_ACTUAL_MHZ].current;
        row["cpu_pkg_temp_c"]   = shm->metrics[MetricId::CPU_PACKAGE_TEMP_C].current;
        row["cpu_core_balance"] = shm->metrics[MetricId::CPU_CORE_BALANCE_SCORE].current;

        // Memory
        row["mem_pct"]          = shm->metrics[MetricId::MEM_PERCENT].current;
        row["mem_used_gb"]      = shm->metrics[MetricId::MEM_USED_GB].current;
        row["mem_avail_gb"]     = shm->metrics[MetricId::MEM_AVAILABLE_GB].current;
        row["mem_standby_gb"]   = shm->metrics[MetricId::MEM_STANDBY_GB].current;
        row["mem_page_faults_s"]= shm->metrics[MetricId::MEM_PAGE_FAULT_RATE].current;
        row["mem_swap_pct"]     = shm->metrics[MetricId::MEM_SWAP_PERCENT].current;

        // GPUs (up to 4)
        json gpus = json::array();
        for (uint32_t gi = 0; gi < shm->hdr.active_gpu_count && gi < 4; ++gi) {
            json g;
            g["idx"]       = gi;
            g["name"]      = shm->hdr.gpu_name[gi];
            g["usage_pct"] = shm->metrics[gpu_metric(gi, GpuOff::USAGE_PCT)].current;
            g["vram_pct"]  = shm->metrics[gpu_metric(gi, GpuOff::VRAM_PCT)].current;
            g["vram_mb"]   = shm->metrics[gpu_metric(gi, GpuOff::VRAM_USED_MB)].current;
            g["temp_c"]    = shm->metrics[gpu_metric(gi, GpuOff::TEMP_C)].current;
            g["power_w"]   = shm->metrics[gpu_metric(gi, GpuOff::POWER_W)].current;
            g["clock_mhz"] = shm->metrics[gpu_metric(gi, GpuOff::CLOCK_CORE_MHZ)].current;
            gpus.push_back(g);
        }
        row["gpu"] = gpus;

        // Disks (up to 8)
        json disks = json::array();
        for (uint32_t di = 0; di < shm->hdr.active_disk_count && di < 8; ++di) {
            json d;
            d["idx"]           = di;
            d["name"]          = shm->hdr.disk_name[di];
            d["read_bytes_s"]  = shm->metrics[disk_metric(di, DiskOff::READ_BYTES_S)].current;
            d["write_bytes_s"] = shm->metrics[disk_metric(di, DiskOff::WRITE_BYTES_S)].current;
            d["busy_pct"]      = shm->metrics[disk_metric(di, DiskOff::BUSY_PCT)].current;
            disks.push_back(d);
        }
        row["disk"] = disks;

        // NICs (up to 8)
        json nics = json::array();
        for (uint32_t ni = 0; ni < shm->hdr.active_nic_count && ni < 8; ++ni) {
            json n;
            n["idx"]          = ni;
            n["name"]         = shm->hdr.nic_name[ni];
            n["recv_bytes_s"] = shm->metrics[net_metric(ni, NetOff::RECV_BYTES_S)].current;
            n["sent_bytes_s"] = shm->metrics[net_metric(ni, NetOff::SENT_BYTES_S)].current;
            nics.push_back(n);
        }
        row["net"] = nics;

        // Self-monitoring
        row["self_cpu_pct"] = shm->metrics[MetricId::SELF_CPU_PCT].current;
        row["power"] = BuildPowerRow(shm);

        // Watched processes (only present when at least one process is being watched)
        if (shm->hdr.active_watch_count > 0) {
            json watched = json::array();
            for (uint32_t slot = 0;
                 slot < shm->hdr.active_watch_count && slot < MetricId::WATCH_MAX_COUNT;
                 ++slot) {
                json w;
                w["slot"]         = slot;
                w["label"]        = shm->hdr.watch_label[slot];
                w["cpu_pct"]      = shm->metrics[watch_metric(slot, WatchOff::CPU_PCT)].current;
                w["mem_private_mb"]= shm->metrics[watch_metric(slot, WatchOff::PRIVATE_MB)].current;
                w["threads"]      = shm->metrics[watch_metric(slot, WatchOff::THREADS)].current;
                w["gpu_sm_pct"]   = shm->metrics[watch_metric(slot, WatchOff::GPU_SM_PCT)].current;
                w["gpu_vram_mb"]  = shm->metrics[watch_metric(slot, WatchOff::GPU_VRAM_MB)].current;
                w["uptime_sec"]   = shm->metrics[watch_metric(slot, WatchOff::UPTIME_S)].current;
                w["page_faults_s"]= shm->metrics[watch_metric(slot, WatchOff::PAGE_FAULTS_S)].current;
                watched.push_back(w);
            }
            row["watched_processes"] = watched;
        }

    } else {
        // ── Filtered row: only the requested metric IDs ────────────────────────
        // Use string keys so the row is self-describing without a schema lookup.
        json metrics = json::object();
        for (uint32_t id : s.metric_ids) {
            if (id < MetricId::METRIC_COUNT) {
                char key[12]{};
                snprintf(key, sizeof(key), "%u", id);
                metrics[key] = shm->metrics[id].current;
            }
        }
        row["metrics"] = metrics;
    }

    s.file << row.dump() << '\n';
    ++s.rows_written;

    // Flush every 10 rows to keep data accessible without sacrificing throughput
    if (s.rows_written % 10 == 0) s.file.flush();
}

} // namespace Service
