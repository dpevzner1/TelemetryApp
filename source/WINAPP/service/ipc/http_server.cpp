#define WIN32_LEAN_AND_MEAN
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <atomic>
#include <string>
#include <vector>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <set>
#include "http_server.h"
#include "shm_writer.h"
#include "../diagnostic_log.h"
#include "../api_key_store.h"
#include "../log_session.h"
#include "../enterprise_config.h"
#include "../hardware_registry.h"
#include "../sensors/power.h"
#include "../process_watcher/watcher.h"
#include "../../shared/shm_layout.h"
#include "../../shared/metric_ids.h"
#include "../../shared/api_types.h"
#include "../../shared/app_version.h"

using json = nlohmann::json;

namespace Service {

static httplib::Server* s_svr = nullptr;
static std::string s_service_url = "http://localhost:8765";

static void ErrResp(httplib::Response& res, int status,
                    const char* error_code, const char* message);

static int64_t NowMs() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u{ ft.dwLowDateTime, ft.dwHighDateTime };
    return static_cast<int64_t>((u.QuadPart - 116444736000000000ULL) / 10000ULL);
}

static bool EnvFlagEnabled(const char* name) {
    const char* v = getenv(name);
    if (!v) return false;
    return _stricmp(v, "1") == 0 ||
           _stricmp(v, "true") == 0 ||
           _stricmp(v, "yes") == 0 ||
           _stricmp(v, "on") == 0;
}

static bool RegDwordFlagEnabled(const char* value_name) {
    HKEY hk{};
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\TelemetryApp", 0,
                      KEY_READ | KEY_WOW64_64KEY, &hk) != ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0;
    DWORD value = 0;
    DWORD len = sizeof(value);
    bool enabled = RegQueryValueExA(hk, value_name, nullptr, &type,
        reinterpret_cast<LPBYTE>(&value), &len) == ERROR_SUCCESS &&
        type == REG_DWORD && value != 0;
    RegCloseKey(hk);
    return enabled;
}

static const char* HttpBindAddress() {
    return (EnvFlagEnabled("TELEMETRY_REMOTE_API") ||
            EnvFlagEnabled("TELEMETRY_REMOTE_API_ENABLED") ||
            RegDwordFlagEnabled("RemoteApiEnabled"))
        ? "0.0.0.0"
        : "127.0.0.1";
}

static std::string AuthKeyFromRequest(const httplib::Request& req) {
    auto key_it = req.headers.find(ApiField::API_KEY_HEADER);
    if (key_it != req.headers.end()) return key_it->second;

    auto auth_it = req.headers.find("Authorization");
    if (auth_it != req.headers.end()) {
        const std::string& v = auth_it->second;
        const std::string prefix = "Bearer ";
        if (v.size() > prefix.size() &&
            _strnicmp(v.c_str(), prefix.c_str(), prefix.size()) == 0) {
            return v.substr(prefix.size());
        }
    }

    if (req.has_param(ApiField::API_KEY_PARAM))
        return req.get_param_value(ApiField::API_KEY_PARAM);

    return {};
}

static bool CheckAuth(const httplib::Request& req, httplib::Response& res) {
    std::string key = AuthKeyFromRequest(req);
    if (key.empty() || !GetKeyStore().Validate(key)) {
        res.status = 401;
        res.set_content(R"({"error":"unauthorized"})", "application/json");
        return false;
    }
    return true;
}

// Read SHM snapshot — returns false if shm unavailable or seqlock torn
static bool IsLocalRequest(const httplib::Request& req) {
    const std::string& addr = req.remote_addr;
    return addr == "127.0.0.1" || addr == "::1" || addr == "localhost";
}

static bool IsAuthorized(const httplib::Request& req) {
    std::string key = AuthKeyFromRequest(req);
    return !key.empty() && GetKeyStore().Validate(key);
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

static json BuildPowerSnapshot(ShmBlock* shm) {
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
            json g;
            std::string name = shm->hdr.gpu_name[gi];
            double watts = shm->metrics[gpu_metric(gi, GpuOff::POWER_W)].current;
            g["index"] = gi;
            g["name"] = name;
            g["power_w"] = GpuPowerReading(name, watts);
            power["gpu"].push_back(g);
        }
    }

    power["process_power"] =
        PowerReading(0.0, "W", "Allocation model", "unavailable", "none",
                     "Per-process electrical attribution is not implemented; current process telemetry supports future estimation");
    return power;
}

static bool ReadShm(json& out) {
    ShmBlock* shm = ShmGet();
    if (!shm) return false;

    uint64_t seq0, seq1;
    int retries = 8;
    do {
        seq0 = shm->hdr.write_seq.load(std::memory_order_acquire);
        if (seq0 & 1) { Sleep(1); continue; }

        out[ApiField::TIMESTAMP] = shm->hdr.ts_poll_ms;

        // CPU
        auto& cpu = out[ApiField::CPU];
        cpu["usage_total_pct"] = shm->metrics[MetricId::CPU_USAGE_TOTAL].current;
        cpu["freq_actual_mhz"] = shm->metrics[MetricId::CPU_FREQ_ACTUAL_MHZ].current;
        json cores = json::array();
        for (uint32_t i = 0; i < shm->hdr.active_cpu_cores && i < 32; ++i)
            cores.push_back(shm->metrics[cpu_core_metric(i)].current);
        cpu["per_core_pct"] = cores;

        // Memory
        auto& mem = out[ApiField::MEMORY];
        mem["total_gb"]       = shm->metrics[MetricId::MEM_TOTAL_GB].current;
        mem["used_gb"]        = shm->metrics[MetricId::MEM_USED_GB].current;
        mem["available_gb"]   = shm->metrics[MetricId::MEM_AVAILABLE_GB].current;
        mem["percent"]        = shm->metrics[MetricId::MEM_PERCENT].current;
        mem["swap_used_gb"]   = shm->metrics[MetricId::MEM_SWAP_USED_GB].current;
        mem["swap_pct"]       = shm->metrics[MetricId::MEM_SWAP_PERCENT].current;
        mem["standby_gb"]     = shm->metrics[MetricId::MEM_STANDBY_GB].current;
        mem["page_fault_rate"]= shm->metrics[MetricId::MEM_PAGE_FAULT_RATE].current;

        // GPUs
        auto& gpus = out[ApiField::GPUS];
        gpus = json::array();
        for (uint32_t gi = 0; gi < shm->hdr.active_gpu_count && gi < 4; ++gi) {
            json g;
            g["index"]           = gi;
            g["name"]            = shm->hdr.gpu_name[gi];  // already UTF-8 char[]
            g["usage_pct"]       = shm->metrics[gpu_metric(gi, GpuOff::USAGE_PCT)].current;
            g["vram_used_mb"]    = shm->metrics[gpu_metric(gi, GpuOff::VRAM_USED_MB)].current;
            g["vram_total_mb"]   = shm->metrics[gpu_metric(gi, GpuOff::VRAM_TOTAL_MB)].current;
            g["vram_pct"]        = shm->metrics[gpu_metric(gi, GpuOff::VRAM_PCT)].current;
            g["temp_c"]          = shm->metrics[gpu_metric(gi, GpuOff::TEMP_C)].current;
            g["power_w"]         = shm->metrics[gpu_metric(gi, GpuOff::POWER_W)].current;
            g["fan_pct"]         = shm->metrics[gpu_metric(gi, GpuOff::FAN_PCT)].current;
            g["clock_core_mhz"]  = shm->metrics[gpu_metric(gi, GpuOff::CLOCK_CORE_MHZ)].current;
            g["clock_mem_mhz"]   = shm->metrics[gpu_metric(gi, GpuOff::CLOCK_MEM_MHZ)].current;
            g["pdh_util_pct"]    = shm->metrics[gpu_metric(gi, GpuOff::PDH_UTIL_PCT)].current;
            g["encoder_pct"]     = shm->metrics[gpu_metric(gi, GpuOff::ENCODER_PCT)].current;
            g["decoder_pct"]     = shm->metrics[gpu_metric(gi, GpuOff::DECODER_PCT)].current;
            g["sm_util_pct"]     = shm->metrics[gpu_metric(gi, GpuOff::SM_UTIL_PCT)].current;
            g["mem_bw_util_pct"] = shm->metrics[gpu_metric(gi, GpuOff::MEM_BW_UTIL_PCT)].current;
            g["mem_clk_transitions"] = shm->metrics[gpu_metric(gi, GpuOff::MEM_CLK_TRANSITIONS)].current;
            g["has_tensor_cores"] = shm->metrics[gpu_metric(gi, GpuOff::TENSOR_ACTIVE)].current > 0.5;
            g["cuda_cc_major"]   = shm->metrics[gpu_metric(gi, GpuOff::CUDA_CC_MAJOR)].current;
            g["cuda_cc_minor"]   = shm->metrics[gpu_metric(gi, GpuOff::CUDA_CC_MINOR)].current;
            g["tensor_core_gen"] = shm->metrics[gpu_metric(gi, GpuOff::TENSOR_CORE_GEN)].current;
            gpus.push_back(g);
        }

        // Disks
        auto& disks = out[ApiField::DISK];
        disks = json::array();
        for (uint32_t di = 0; di < shm->hdr.active_disk_count && di < 8; ++di) {
            json d;
            d["name"]          = shm->hdr.disk_name[di];
            d["read_bytes_s"]  = shm->metrics[disk_metric(di, DiskOff::READ_BYTES_S)].current;
            d["write_bytes_s"] = shm->metrics[disk_metric(di, DiskOff::WRITE_BYTES_S)].current;
            d["read_iops"]     = shm->metrics[disk_metric(di, DiskOff::READ_IOPS)].current;
            d["write_iops"]    = shm->metrics[disk_metric(di, DiskOff::WRITE_IOPS)].current;
            d["busy_pct"]      = shm->metrics[disk_metric(di, DiskOff::BUSY_PCT)].current;
            disks.push_back(d);
        }

        // NICs
        auto& nics = out[ApiField::NETWORK];
        nics = json::array();
        for (uint32_t ni = 0; ni < shm->hdr.active_nic_count && ni < 8; ++ni) {
            json n;
            n["name"]         = shm->hdr.nic_name[ni];
            n["recv_bytes_s"] = shm->metrics[net_metric(ni, NetOff::RECV_BYTES_S)].current;
            n["sent_bytes_s"] = shm->metrics[net_metric(ni, NetOff::SENT_BYTES_S)].current;
            n["recv_pkts_s"]  = shm->metrics[net_metric(ni, NetOff::RECV_PKTS_S)].current;
            n["sent_pkts_s"]  = shm->metrics[net_metric(ni, NetOff::SENT_PKTS_S)].current;
            nics.push_back(n);
        }

        // Self-monitoring
        out["self"]["cpu_pct"] = shm->metrics[MetricId::SELF_CPU_PCT].current;

        out[ApiField::POWER] = BuildPowerSnapshot(shm);

        auto& watched = out[ApiField::WATCHED];
        watched = json::array();
        for (uint32_t slot = 0; slot < shm->hdr.active_watch_count &&
                              slot < MetricId::WATCH_MAX_COUNT; ++slot) {
            json w;
            w["slot"] = slot;
            w["label"] = shm->hdr.watch_label[slot];
            w["cpu_pct"] = shm->metrics[watch_metric(slot, WatchOff::CPU_PCT)].current;
            w["mem_private_mb"] = shm->metrics[watch_metric(slot, WatchOff::PRIVATE_MB)].current;
            w["gpu_sm_pct"] = shm->metrics[watch_metric(slot, WatchOff::GPU_SM_PCT)].current;
            w["gpu_vram_mb"] = shm->metrics[watch_metric(slot, WatchOff::GPU_VRAM_MB)].current;
            w["disk_read_bytes_s"] = shm->metrics[watch_metric(slot, WatchOff::DISK_READ_BYTES_S)].current;
            w["disk_write_bytes_s"] = shm->metrics[watch_metric(slot, WatchOff::DISK_WRITE_BYTES_S)].current;
            w["page_faults_s"] = shm->metrics[watch_metric(slot, WatchOff::PAGE_FAULTS_S)].current;
            w["uptime_sec"] = shm->metrics[watch_metric(slot, WatchOff::UPTIME_S)].current;
            w["threads"] = shm->metrics[watch_metric(slot, WatchOff::THREADS)].current;
            w["handles"] = shm->metrics[watch_metric(slot, WatchOff::HANDLES)].current;
            watched.push_back(w);
        }

        seq1 = shm->hdr.write_seq.load(std::memory_order_acquire);
        if (seq1 == seq0) return true; // clean read
    } while (--retries > 0);
    return false;
}

static bool LabEnrollmentAllowed(httplib::Response& res) {
    const auto& cfg = GetEnterpriseConfig();
    if (cfg.enrollment_state == "enrolled_lab") return true;
    res.status = 403;
    res.set_content(R"({"error":"lab enrollment required"})", "application/json");
    DiagnosticLogWarn("Rejected lab remote request before explicit enrollment.");
    return false;
}

static std::string FleetDevicesPath() {
    return GetEnterpriseConfig().data_dir + "\\fleet_devices.json";
}

static bool IsFleetHostMode() {
    const auto& mode = GetEnterpriseConfig().install_mode;
    return mode == "FleetHost";
}

static void AppendUniqueAddress(json& row, const std::string& address) {
    if (address.empty()) return;
    json history = row.value("address_history", json::array());
    if (!history.is_array()) history = json::array();
    json next = json::array();
    bool seen = false;
    for (const auto& v : history) {
        if (!v.is_string()) continue;
        std::string s = v.get<std::string>();
        if (s == address) seen = true;
        if (next.size() < 12) next.push_back(s);
    }
    if (!seen) next.push_back(address);
    while (next.size() > 12) next.erase(next.begin());
    row["address_history"] = next;
}

static bool SameFleetDevice(const json& row,
                            const std::string& device_id,
                            const std::string& sensor_hash,
                            const std::string& mac_hash,
                            const std::string& address) {
    return (!device_id.empty() && row.value("device_id", "") == device_id) ||
           (!sensor_hash.empty() && row.value("sensor_hash", "") == sensor_hash) ||
           (!mac_hash.empty() && row.value("mac_hash", "") == mac_hash) ||
           (!address.empty() && row.value("address", "") == address);
}

static bool MergeFleetHeartbeat(const httplib::Request& req,
                                const json& body,
                                json& response) {
    int api_port = body.value("api_port", HTTP_PORT);
    if (api_port <= 0 || api_port > 65535) api_port = HTTP_PORT;
    std::string remote = req.remote_addr;
    if (remote == "::ffff:127.0.0.1") remote = "127.0.0.1";
    if (remote.rfind("::ffff:", 0) == 0) remote = remote.substr(7);
    std::string address = remote + ":" + std::to_string(api_port);
    std::string sensor_hash = body.value("sensor_id_hash", "");
    std::string device_id = body.value("device_id", sensor_hash);
    std::string mac_hash = body.value("mac_hash", "");
    std::string hostname = body.value("hostname", "");
    std::string role = body.value("install_mode", "SensorClient");
    std::string enrollment_state = body.value("enrollment_state", "not_enrolled");
    const int64_t now = NowMs();

    json root;
    {
        std::ifstream f(FleetDevicesPath());
        if (f.is_open()) root = json::parse(f, nullptr, false);
    }
    if (!root.is_object()) root = json::object();
    root["schema"] = 2;
    root["truth_model"] = "device_id/sensor_hash and mac_hash reconcile IP churn; heartbeat creates candidates but never auto-trusts";
    if (!root.contains("devices") || !root["devices"].is_array()) root["devices"] = json::array();

    bool matched = false;
    bool trusted = false;
    for (auto& row : root["devices"]) {
        if (!row.is_object()) continue;
        if (!SameFleetDevice(row, device_id, sensor_hash, mac_hash, address)) continue;
        matched = true;
        trusted = row.value("trusted", false);
        AppendUniqueAddress(row, row.value("address", ""));
        AppendUniqueAddress(row, address);
        row["name"] = hostname.empty() ? row.value("name", "TelemetryApp Sensor") : hostname;
        row["hostname"] = hostname;
        row["role"] = role;
        row["address"] = address;
        row["last_seen_address"] = address;
        row["last_seen_at_ms"] = now;
        row["state"] = "Online";
        row["os"] = "Windows";
        row["device_id"] = device_id;
        row["sensor_hash"] = sensor_hash;
        row["mac_hash"] = mac_hash;
        row["enrollment_state"] = enrollment_state;
        row["trusted"] = trusted;
        row["last_error"] = "";
        break;
    }

    if (!matched) {
        json row;
        row["name"] = hostname.empty() ? ("Sensor " + sensor_hash) : hostname;
        row["hostname"] = hostname;
        row["role"] = role;
        row["address"] = address;
        row["last_seen_address"] = address;
        row["last_seen_at_ms"] = now;
        row["state"] = "Online";
        row["os"] = "Windows";
        row["device_id"] = device_id;
        row["sensor_hash"] = sensor_hash;
        row["mac_hash"] = mac_hash;
        row["enrollment_state"] = enrollment_state;
        row["trusted"] = false;
        row["last_error"] = "";
        row["address_history"] = json::array({address});
        root["devices"].push_back(row);
    }

    std::ofstream out(FleetDevicesPath(), std::ios::trunc);
    if (!out.is_open()) return false;
    out << root.dump(2);

    response["accepted"] = true;
    response["matched_existing"] = matched;
    response["trusted"] = trusted;
    response["device_id"] = device_id;
    response["sensor_id_hash"] = sensor_hash;
    response["mac_hash"] = mac_hash;
    response["address"] = address;
    response["message"] = matched
        ? "Heartbeat merged into existing fleet device record."
        : "Heartbeat created an untrusted fleet candidate.";
    return true;
}

static bool ParseSessionStartBody(const httplib::Request& req,
                                  std::string& label,
                                  std::string& log_dir,
                                  std::vector<uint32_t>& metric_ids,
                                  int64_t& duration_ms,
                                  bool& stop_when_watch_empty,
                                  std::vector<uint32_t>& watch_pids,
                                  std::vector<std::string>& watch_exes,
                                  httplib::Response& res) {
    duration_ms = 0;
    stop_when_watch_empty = false;
    try {
        json body = json::parse(req.body.empty() ? "{}" : req.body);
        label   = body.value("label", "");
        log_dir = body.value("log_dir", "");
        int64_t duration_sec = body.value("duration_sec", static_cast<int64_t>(0));
        if (body.contains("stop_policy") && body["stop_policy"].is_object()) {
            const auto& sp = body["stop_policy"];
            std::string mode = sp.value("mode", "");
            if (sp.contains("max_duration_seconds"))
                duration_sec = sp.value("max_duration_seconds", duration_sec);
            stop_when_watch_empty =
                mode == "process_exit" || mode == "process_exit_or_duration";
        }
        if (duration_sec > 0) duration_ms = duration_sec * 1000;
        if (body.contains("metric_ids"))
            for (auto& v : body["metric_ids"])
                metric_ids.push_back(v.get<uint32_t>());
        if (body.contains("watch") && body["watch"].is_object()) {
            const auto& w = body["watch"];
            if (w.contains("pids"))
                for (auto& v : w["pids"])
                    watch_pids.push_back(v.get<uint32_t>());
            if (w.contains("exe_names"))
                for (auto& v : w["exe_names"])
                    watch_exes.push_back(v.get<std::string>());
        }
    } catch (...) {
        ErrResp(res, 400, "ERR_INVALID_BODY", "Request body must be valid JSON");
        return false;
    }
    if (!log_dir.empty()) {
        bool is_abs = (log_dir.size() >= 3 &&
                       isalpha((unsigned char)log_dir[0]) && log_dir[1] == ':')
                   || (log_dir.size() >= 2 &&
                       log_dir[0] == '\\' && log_dir[1] == '\\');
        if (!is_abs) {
            ErrResp(res, 400, "ERR_INVALID_LOG_DIR",
                    "log_dir must be an absolute Windows path (e.g. C:\\MyLogs or \\\\server\\share)");
            return false;
        }
    }
    return true;
}

static void StartSessionFromParsedRequest(const std::string& label,
                                          const std::string& log_dir,
                                          const std::vector<uint32_t>& metric_ids,
                                          int64_t duration_ms,
                                          bool stop_when_watch_empty,
                                          const std::vector<uint32_t>& watch_pids,
                                          const std::vector<std::string>& watch_exes,
                                          httplib::Response& res) {
    for (uint32_t pid : watch_pids) GetWatcher().AddPid(pid);
    for (const auto& exe : watch_exes) GetWatcher().AddWatch(exe, label.empty() ? exe : label);

    std::string sid = LogSessionStore::Instance().Start(
        label, metric_ids, log_dir, duration_ms, stop_when_watch_empty);
    if (sid.empty()) {
        ErrResp(res, 500, "ERR_SESSION_START_FAILED",
                "Failed to open log file - check log_dir write access or TELEMETRY_DATA_DIR");
        return;
    }
    LogSessionStore::SessionSummary s;
    LogSessionStore::Instance().FindSummary(sid, s);
    json resp;
    resp["session_id"]    = sid;
    resp["label"]         = s.label;
    resp["started_at_ms"] = s.started_at_ms;
    resp["log_dir"]       = s.log_dir;
    resp["log_path"]      = s.log_path;
    resp["duration_ms"]   = s.duration_ms;
    resp["stop_when_watch_empty"] = s.stop_when_watch_empty;
    resp["metric_ids"]    = s.metric_ids;
    res.status = 201;
    res.set_content(resp.dump(), "application/json");
}

// Prometheus text format
static std::string BuildPrometheus() {
    ShmBlock* shm = ShmGet();
    if (!shm) return "# SHM unavailable\n";

    uint64_t seq0;
    do { seq0 = shm->hdr.write_seq.load(std::memory_order_acquire); } while (seq0 & 1);

    std::string out;
    out.reserve(8192);
    auto esc = [](const char* raw) {
        std::string src = raw ? raw : "";
        std::string dst;
        dst.reserve(src.size());
        for (char c : src) {
            if (c == '\\' || c == '"') dst.push_back('\\');
            dst.push_back(c);
        }
        return dst;
    };
    auto g = [&](const char* name, double v, const char* labels = "") {
        out += "# TYPE "; out += name; out += " gauge\n";
        out += name;
        if (labels && labels[0]) { out += '{'; out += labels; out += '}'; }
        out += ' ';
        out += std::to_string(v);
        out += '\n';
    };

    char lbl[192];
    g(PromName::CPU_USAGE,    shm->metrics[MetricId::CPU_USAGE_TOTAL].current);
    g(PromName::CPU_FREQ_MHZ, shm->metrics[MetricId::CPU_FREQ_ACTUAL_MHZ].current);
    {
        double cpu_pkg_w = shm->metrics[MetricId::CPU_PACKAGE_POWER_W].current;
        snprintf(lbl, sizeof(lbl), "source=\"CPU package provider\",quality=\"%s\"",
                 cpu_pkg_w > 0.0 ? "measured" : "unavailable");
        g("telemetry_cpu_package_power_watts", cpu_pkg_w, lbl);
        g("telemetry_platform_power_watts",
          0.0,
          "source=\"Software-only platform provider\",quality=\"unavailable\"");
    }
    g(PromName::MEM_TOTAL,    shm->metrics[MetricId::MEM_TOTAL_GB].current * 1024.0 * 1024.0 * 1024.0);
    g(PromName::MEM_USED,     shm->metrics[MetricId::MEM_USED_GB].current * 1024.0 * 1024.0 * 1024.0);
    g(PromName::MEM_AVAIL,    shm->metrics[MetricId::MEM_AVAILABLE_GB].current * 1024.0 * 1024.0 * 1024.0);
    g(PromName::MEM_PCT,      shm->metrics[MetricId::MEM_PERCENT].current);

    for (uint32_t gi = 0; gi < shm->hdr.active_gpu_count && gi < 4; ++gi) {
        std::string name = esc(shm->hdr.gpu_name[gi]);
        snprintf(lbl, sizeof(lbl), "gpu=\"%u\",name=\"%s\"", gi, name.c_str());
        g(PromName::GPU_USAGE,     shm->metrics[gpu_metric(gi, GpuOff::USAGE_PCT)].current,  lbl);
        g(PromName::GPU_MEM_USED,  shm->metrics[gpu_metric(gi, GpuOff::VRAM_USED_MB)].current, lbl);
        g(PromName::GPU_MEM_TOTAL, shm->metrics[gpu_metric(gi, GpuOff::VRAM_TOTAL_MB)].current, lbl);
        g(PromName::GPU_TEMP,      shm->metrics[gpu_metric(gi, GpuOff::TEMP_C)].current,     lbl);
        {
            double watts = shm->metrics[gpu_metric(gi, GpuOff::POWER_W)].current;
            std::string source = GpuPowerSource(name);
            std::string quality = watts > 0.0 ? "measured" : "unavailable";
            snprintf(lbl, sizeof(lbl), "gpu=\"%u\",name=\"%s\",source=\"%s\",quality=\"%s\"",
                     gi, name.c_str(), esc(source.c_str()).c_str(), quality.c_str());
            g(PromName::GPU_POWER, watts, lbl);
        }
        snprintf(lbl, sizeof(lbl), "gpu=\"%u\",name=\"%s\"", gi, name.c_str());
        g(PromName::GPU_THROTTLE,  shm->metrics[gpu_metric(gi, GpuOff::THROTTLE_ACTIVE)].current, lbl);
        g(PromName::GPU_PDH_USAGE, shm->metrics[gpu_metric(gi, GpuOff::PDH_UTIL_PCT)].current, lbl);
    }

    for (uint32_t di = 0; di < shm->hdr.active_disk_count && di < 8; ++di) {
        std::string name = esc(shm->hdr.disk_name[di]);
        snprintf(lbl, sizeof(lbl), "device=\"%u\",name=\"%s\"", di, name.c_str());
        g(PromName::DISK_READ,  shm->metrics[disk_metric(di, DiskOff::READ_BYTES_S)].current, lbl);
        g(PromName::DISK_WRITE, shm->metrics[disk_metric(di, DiskOff::WRITE_BYTES_S)].current, lbl);
        g(PromName::DISK_BUSY,  shm->metrics[disk_metric(di, DiskOff::BUSY_PCT)].current, lbl);
    }

    for (uint32_t ni = 0; ni < shm->hdr.active_nic_count && ni < 8; ++ni) {
        std::string name = esc(shm->hdr.nic_name[ni]);
        snprintf(lbl, sizeof(lbl), "nic=\"%u\",name=\"%s\"", ni, name.c_str());
        g(PromName::NET_RECV, shm->metrics[net_metric(ni, NetOff::RECV_BYTES_S)].current, lbl);
        g(PromName::NET_SENT, shm->metrics[net_metric(ni, NetOff::SENT_BYTES_S)].current, lbl);
    }

    for (uint32_t slot = 0; slot < shm->hdr.active_watch_count &&
                          slot < MetricId::WATCH_MAX_COUNT; ++slot) {
        std::string label = esc(shm->hdr.watch_label[slot]);
        snprintf(lbl, sizeof(lbl), "slot=\"%u\",label=\"%s\"", slot, label.c_str());
        g(PromName::PROC_CPU,     shm->metrics[watch_metric(slot, WatchOff::CPU_PCT)].current, lbl);
        g(PromName::PROC_RSS,     shm->metrics[watch_metric(slot, WatchOff::PRIVATE_MB)].current * 1024.0 * 1024.0, lbl);
        g(PromName::PROC_GPU_SM,  shm->metrics[watch_metric(slot, WatchOff::GPU_SM_PCT)].current, lbl);
        g(PromName::PROC_GPU_PDH, shm->metrics[watch_metric(slot, WatchOff::GPU_PDH_PCT)].current, lbl);
        g(PromName::PROC_THREADS, shm->metrics[watch_metric(slot, WatchOff::THREADS)].current, lbl);
        g(PromName::PROC_HANDLES, shm->metrics[watch_metric(slot, WatchOff::HANDLES)].current, lbl);
    }

    g(PromName::SELF_CPU,     shm->metrics[MetricId::SELF_CPU_PCT].current);
    g(PromName::SELF_POLL_MS, static_cast<double>(shm->hdr.poll_duration_ms));

    uint64_t seq1 = shm->hdr.write_seq.load(std::memory_order_acquire);
    if (seq1 != seq0) return "# torn read\n";
    return out;
}

// Structured error response (DevOps engine pattern)
static void ErrResp(httplib::Response& res, int status,
                    const char* error_code, const char* message) {
    res.status = status;
    json j;
    j["error"]   = error_code;
    j["message"] = message;
    j["ts_ms"]   = static_cast<int64_t>(
        []{ FILETIME ft{}; GetSystemTimeAsFileTime(&ft);
            ULARGE_INTEGER u{ft.dwLowDateTime, ft.dwHighDateTime};
            return (u.QuadPart - 116444736000000000ULL) / 10000ULL; }());
    res.set_content(j.dump(), "application/json");
}

static json KeyToJson(const ApiKey& k) {
    json j;
    j["id"]           = k.id;
    j["name"]         = k.name;
    j["key_prefix"]   = k.key_prefix;
    j["created_at"]   = k.created_at;
    j["expiry_type"]  = static_cast<int>(k.expiry_type);
    j["expires_at"]   = k.expires_at;
    j["active"]       = k.active;
    j["status"]       = k.active ? "Active" : "Revoked";
    return j;
}

bool HttpServerInit() {
    DiagnosticLogInfo("HttpServerInit begin.");
    s_svr = new httplib::Server();
    if (!s_svr) {
        DiagnosticLogError("HttpServerInit failed: allocation returned null.");
        return false;
    }

    s_svr->set_logger([](const httplib::Request& req, const httplib::Response& res) {
        DiagnosticLogInfo("HTTP " + req.method + " " + req.path +
            " status=" + std::to_string(res.status));
    });

    // Retrieve service URL from env for API.md generation
    const char* url_env = getenv("TELEMETRY_API_URL");
    if (url_env) s_service_url = url_env;

    // GET /api/v1/snapshot  — full current snapshot
    s_svr->Get("/api/v1/snapshot", [](const httplib::Request& req, httplib::Response& res) {
        if (!CheckAuth(req, res)) return;
        json snap;
        if (ReadShm(snap))
            res.set_content(snap.dump(), "application/json");
        else
            res.set_content(R"({"error":"shm unavailable"})", "application/json");
    });

    // GET /api/v1/history/<metric_id>?n=300  — last N values from ring
    s_svr->Get("/api/v1/capabilities", [](const httplib::Request& req, httplib::Response& res) {
        if (!CheckAuth(req, res)) return;
        ShmBlock* shm = ShmGet();
        json j;
        j["app"] = {
            {"name", "TelemetryApp"},
            {"version", TelemetryApp::APP_VERSION},
            {"build_date", TelemetryApp::BUILD_DATE},
            {"capability_revision", TelemetryApp::CAPABILITY_REVISION},
            {"docs_bundle", TelemetryApp::DOCS_BUNDLE}
        };
        j["shared_memory"] = shm != nullptr;
        j["service_alive"] = shm && shm->hdr.service_alive;
        j["active_cpu_cores"] = shm ? shm->hdr.active_cpu_cores : 0;
        j["active_gpu_count"] = shm ? shm->hdr.active_gpu_count : 0;
        j["accelerators"] = json::array();
        if (shm) {
            for (uint32_t gi = 0; gi < shm->hdr.active_gpu_count && gi < 4; ++gi) {
                json a;
                a["index"] = gi;
                a["name"] = shm->hdr.gpu_name[gi];
                {
                    std::string name = shm->hdr.gpu_name[gi];
                    double watts = shm->metrics[gpu_metric(gi, GpuOff::POWER_W)].current;
                    a["power"] = {
                        {"metric_id", gpu_metric(gi, GpuOff::POWER_W)},
                        {"source", GpuPowerSource(name)},
                        {"quality", watts > 0.0 ? "measured" : "unavailable"},
                        {"confidence", watts > 0.0 ? "high" : "none"}
                    };
                }
                a["has_tensor_cores"] = shm->metrics[gpu_metric(gi, GpuOff::TENSOR_ACTIVE)].current > 0.5;
                a["cuda_cc_major"] = shm->metrics[gpu_metric(gi, GpuOff::CUDA_CC_MAJOR)].current;
                a["cuda_cc_minor"] = shm->metrics[gpu_metric(gi, GpuOff::CUDA_CC_MINOR)].current;
                a["tensor_core_gen"] = shm->metrics[gpu_metric(gi, GpuOff::TENSOR_CORE_GEN)].current;
                a["sm_util_metric_id"] = gpu_metric(gi, GpuOff::SM_UTIL_PCT);
                a["mem_bw_metric_id"] = gpu_metric(gi, GpuOff::MEM_BW_UTIL_PCT);
                j["accelerators"].push_back(a);
            }
        }
        j["active_disk_count"] = shm ? shm->hdr.active_disk_count : 0;
        j["active_nic_count"] = shm ? shm->hdr.active_nic_count : 0;
        j["active_temp_count"] = shm ? shm->hdr.active_temp_count : 0;
        j["active_watch_count"] = shm ? shm->hdr.active_watch_count : 0;
        j["power_telemetry"] = {
            {"truth_model", "measured > derived > estimated > unavailable"},
            {"cpu_package_power", shm && shm->metrics[MetricId::CPU_PACKAGE_POWER_W].current > 0.0 ? "measured" : "unavailable"},
            {"platform_power", "derived_on_battery_discharge_when_windows_reports_battery_rate; unavailable_on_ac_without_external_meter"},
            {"process_power", "future_estimated_attribution"}
        };
        j["auth"] = {"X-API-Key", "Authorization: Bearer", "api_key query parameter"};
        res.set_content(j.dump(), "application/json");
    });

    s_svr->Get("/api/v1/hardware", [](const httplib::Request& req, httplib::Response& res) {
        if (!CheckAuth(req, res)) return;
        res.set_content(BuildHardwareInventoryJson(ShmGet()).dump(), "application/json");
    });

    s_svr->Get("/api/v1/metrics/catalog", [](const httplib::Request& req, httplib::Response& res) {
        if (!CheckAuth(req, res)) return;
        json ids = json::array();
        auto add = [&](uint32_t id, const char* name, const char* unit, const char* group) {
            ids.push_back({{"id", id}, {"name", name}, {"unit", unit}, {"group", group}});
        };
        add(MetricId::CPU_USAGE_TOTAL, "cpu_usage_total", "percent", "cpu");
        add(MetricId::CPU_FREQ_ACTUAL_MHZ, "cpu_freq_actual", "MHz", "cpu");
        add(MetricId::CPU_PACKAGE_POWER_W, "cpu_package_power", "watts", "power");
        add(MetricId::MEM_TOTAL_GB, "memory_total", "GB", "memory");
        add(MetricId::MEM_USED_GB, "memory_used", "GB", "memory");
        add(MetricId::MEM_PERCENT, "memory_usage", "percent", "memory");
        add(gpu_metric(0, GpuOff::USAGE_PCT), "gpu0_usage", "percent", "gpu");
        add(gpu_metric(0, GpuOff::VRAM_USED_MB), "gpu0_vram_used", "MB", "gpu");
        add(gpu_metric(0, GpuOff::TEMP_C), "gpu0_temp", "celsius", "gpu");
        add(gpu_metric(0, GpuOff::POWER_W), "gpu0_power", "watts", "power");
        add(gpu_metric(0, GpuOff::SM_UTIL_PCT), "gpu0_sm_util", "percent", "gpu");
        add(gpu_metric(0, GpuOff::MEM_BW_UTIL_PCT), "gpu0_mem_bw_util", "percent", "gpu");
        add(gpu_metric(0, GpuOff::TENSOR_ACTIVE), "gpu0_tensor_cores_present", "boolean", "gpu");
        add(gpu_metric(0, GpuOff::CUDA_CC_MAJOR), "gpu0_cuda_cc_major", "count", "gpu");
        add(gpu_metric(0, GpuOff::CUDA_CC_MINOR), "gpu0_cuda_cc_minor", "count", "gpu");
        add(gpu_metric(0, GpuOff::TENSOR_CORE_GEN), "gpu0_tensor_core_gen", "count", "gpu");
        add(MetricId::WATCH_BASE, "watch0_cpu", "percent", "process");
        add(watch_metric(0, WatchOff::PRIVATE_MB), "watch0_private_memory", "MB", "process");
        add(watch_metric(0, WatchOff::GPU_SM_PCT), "watch0_gpu_sm", "percent", "process");
        json j;
        j["schema_version"] = SHM_VERSION;
        j["metric_count"] = MetricId::METRIC_COUNT;
        j["common_ids"] = ids;
        j["notes"] = "Indexed ranges are defined in shared/metric_ids.h; IDs are append-only.";
        res.set_content(j.dump(), "application/json");
    });

    s_svr->Get("/api/v1/diagnostics", [](const httplib::Request& req, httplib::Response& res) {
        if (!CheckAuth(req, res)) return;
        ShmBlock* shm = ShmGet();
        WatchConfig cfg = GetWatcher().GetConfig();
        json j;
        j["service_alive"] = shm && shm->hdr.service_alive;
        j["shared_memory"] = shm != nullptr;
        j["poll_duration_ms"] = shm ? shm->hdr.poll_duration_ms : 0;
        j["active_cpu_cores"] = shm ? shm->hdr.active_cpu_cores : 0;
        j["active_gpu_count"] = shm ? shm->hdr.active_gpu_count : 0;
        j["active_disk_count"] = shm ? shm->hdr.active_disk_count : 0;
        j["active_nic_count"] = shm ? shm->hdr.active_nic_count : 0;
        j["active_watch_count"] = shm ? shm->hdr.active_watch_count : 0;
        j["configured_watch_exe_count"] = cfg.exe_names.size();
        j["configured_watch_pid_count"] = cfg.pids.size();
        j["live_watch_entry_count"] = GetWatcher().EntriesSnapshot().size();
        j["api_key_count"] = GetKeyStore().Keys().size();
        j["session_count"] = LogSessionStore::Instance().List().size();
        j["build"] = "native-winapp";
        res.set_content(j.dump(), "application/json");
    });

    s_svr->Get(R"(/api/v1/history/(\d+))", [](const httplib::Request& req, httplib::Response& res) {
        if (!CheckAuth(req, res)) return;
        uint32_t mid = static_cast<uint32_t>(std::stoul(req.matches[1]));
        int n = 300;
        if (req.has_param("n")) n = std::stoi(req.get_param_value("n"));
        n = std::max(1, std::min(n, static_cast<int>(MetricRing::DEPTH)));

        ShmBlock* shm = ShmGet();
        if (!shm || mid >= MetricId::METRIC_COUNT) {
            res.set_content(R"({"error":"invalid"})", "application/json"); return;
        }
        double vals[MetricRing::DEPTH];
        uint32_t got = shm->metrics[mid].read_last(vals, n);
        json j = json::array();
        for (uint32_t i = 0; i < got; ++i) j.push_back(vals[i]);
        res.set_content(j.dump(), "application/json");
    });

    // GET /metrics  — Prometheus scrape endpoint (no auth, standard)
    s_svr->Get("/metrics", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(BuildPrometheus(), "text/plain; version=0.0.4");
    });

    // GET /api/v1/stream  — SSE real-time push (~1Hz)
    s_svr->Get("/api/v1/stream", [](const httplib::Request& req, httplib::Response& res) {
        if (!CheckAuth(req, res)) return;
        res.set_chunked_content_provider("text/event-stream",
            [](size_t, httplib::DataSink& sink) {
                json snap;
                if (ReadShm(snap)) {
                    std::string msg = "data: " + snap.dump() + "\n\n";
                    sink.write(msg.data(), msg.size());
                }
                Sleep(1000);
                return true; // keep stream alive
            });
    });

    // GET /api/v1/health
    s_svr->Get("/api/v1/health", [](const httplib::Request&, httplib::Response& res) {
        ShmBlock* shm = ShmGet();
        json j;
        j["alive"] = (shm && shm->hdr.service_alive);
        if (shm) j["poll_duration_ms"] = shm->hdr.poll_duration_ms;
        res.set_content(j.dump(), "application/json");
    });

    // ── API Key Management ─────────────────────────────────────────────────────

    // GET /api/v1/enrollment/readiness
    // Public, non-secret readiness metadata for manual add / candidate discovery.
    s_svr->Get("/api/v1/enrollment/readiness", [](const httplib::Request&, httplib::Response& res) {
        json j = EnterpriseConfigJson(true);
        j["status"] = "ready_for_explicit_enrollment";
        j["trust_required"] = true;
        j["notes"] = "Candidate discovery is not trust. Enrollment must validate a token and pinned device identity.";
        res.set_content(j.dump(), "application/json");
    });

    // POST /api/v1/enrollment/request
    // Explicit lab enrollment contract. This is operator-driven trust for LAN/lab use,
    // not the future enterprise TLS/mTLS enrollment provider.
    s_svr->Post("/api/v1/enrollment/request", [](const httplib::Request& req, httplib::Response& res) {
        try {
            json body = json::parse(req.body);
            json out;
            json local = EnterpriseConfigJson(true);
            const std::string expected_sensor = local.value("sensor_id_hash", "");
            const std::string expected_mac = local.value("mac_hash", "");
            const std::string requested_sensor = body.value("sensor_id_hash", "");
            const std::string requested_mac = body.value("mac_hash", "");
            const bool explicit_accept = body.value("accept_lab_enrollment", false);
            const bool sensor_match = !expected_sensor.empty() && requested_sensor == expected_sensor;
            const bool mac_match = expected_mac.empty() || requested_mac.empty() || requested_mac == expected_mac;

            if (!explicit_accept || !sensor_match || !mac_match) {
                out["accepted"] = false;
                out["state"] = "rejected";
                out["sensor_id_hash"] = expected_sensor;
                out["message"] = "Enrollment rejected. Explicit lab enrollment plus matching sensor fingerprint are required.";
                res.status = 400;
                res.set_content(out.dump(), "application/json");
                DiagnosticLogWarn("Lab enrollment rejected: explicit=" +
                                  std::string(explicit_accept ? "true" : "false") +
                                  ", sensor_match=" + (sensor_match ? "true" : "false") +
                                  ", mac_match=" + (mac_match ? "true" : "false"));
                return;
            }

            std::string host_name = body.value("host_name", "TelemetryApp Fleet Host");
            std::string host_instance = body.value("host_instance", "");
            std::string host_address = body.value("host_address", req.remote_addr);
            RecordLabEnrollment(host_name, host_instance, host_address);

            out = EnterpriseConfigJson(true);
            out["accepted"] = true;
            out["state"] = "enrolled_lab";
            out["message"] = "Explicit lab enrollment accepted. Enterprise TLS/mTLS and certificate pinning remain a planned hardening step.";
            res.status = 200;
            res.set_content(out.dump(), "application/json");
            DiagnosticLogInfo("Explicit lab enrollment accepted for host=" + host_name);
        } catch (...) {
            res.status = 400;
            res.set_content(R"({"error":"invalid enrollment body"})", "application/json");
        }
    });

    // POST /api/v1/fleet/heartbeat
    // Sensor call-home keeps fleet inventory current after DHCP/IP changes.
    // It never grants trust; it only creates or updates candidate records.
    s_svr->Post("/api/v1/fleet/heartbeat", [](const httplib::Request& req, httplib::Response& res) {
        if (!IsFleetHostMode()) {
            ErrResp(res, 403, "ERR_NOT_FLEET_HOST", "Fleet heartbeat is accepted only by a Fleet Manager install.");
            return;
        }
        json body = json::parse(req.body.empty() ? "{}" : req.body, nullptr, false);
        if (!body.is_object() || body.value("product", "") != "TelemetryApp") {
            ErrResp(res, 400, "ERR_INVALID_HEARTBEAT", "Heartbeat body must be TelemetryApp JSON.");
            return;
        }
        json out;
        if (!MergeFleetHeartbeat(req, body, out)) {
            ErrResp(res, 500, "ERR_HEARTBEAT_STORE", "Fleet heartbeat could not be persisted.");
            return;
        }
        res.status = 202;
        res.set_content(out.dump(), "application/json");
    });

    // GET /api/v1/install/audit
    s_svr->Get("/api/v1/install/audit", [](const httplib::Request& req, httplib::Response& res) {
        if (!CheckAuth(req, res)) return;
        res.set_content(InstallAuditJson().dump(), "application/json");
    });

    // GET /api/v1/keys
    s_svr->Get("/api/v1/keys", [](const httplib::Request& req, httplib::Response& res) {
        if (!CheckAuth(req, res)) return;
        json arr = json::array();
        for (const auto& k : GetKeyStore().Keys())
            arr.push_back(KeyToJson(k));
        res.set_content(arr.dump(), "application/json");
    });

    // POST /api/v1/keys  — body: {"name":"...","expiry_type":0,"expires_at":0}
    s_svr->Post("/api/v1/keys", [](const httplib::Request& req, httplib::Response& res) {
        if (!GetKeyStore().Keys().empty() && !IsAuthorized(req)) {
            if (!IsLocalRequest(req)) {
                res.status = 401;
                res.set_content(R"({"error":"unauthorized"})", "application/json");
                DiagnosticLogWarn("Rejected remote unauthenticated API key creation request.");
                return;
            }
            DiagnosticLogWarn("Allowed local unauthenticated API key creation for desktop bootstrap.");
        }
        try {
            json body = json::parse(req.body);
            std::string name       = body.value("name", "Unnamed");
            int expiry_type        = body.value("expiry_type", 0);
            int64_t custom_expiry  = body.value("expires_at", (int64_t)0);
            std::string plaintext  = GetKeyStore().Create(
                name, static_cast<ExpiryType>(expiry_type), custom_expiry);
            // Regenerate API.md after key change
            GetKeyStore().GenerateApiMd(s_service_url);
            json resp;
            resp["key"]    = plaintext;  // plaintext returned once
            resp["prefix"] = plaintext.substr(0, 12);
            res.set_content(resp.dump(), "application/json");
            DiagnosticLogInfo("API key created: name=" + name + ", prefix=" + plaintext.substr(0, 12));
        } catch (...) {
            res.status = 400;
            res.set_content(R"({"error":"invalid body"})", "application/json");
            DiagnosticLogWarn("API key creation failed: invalid request body.");
        }
    });

    // DELETE /api/v1/keys/<id>
    s_svr->Delete(R"(/api/v1/keys/([a-f0-9-]+))",
        [](const httplib::Request& req, httplib::Response& res) {
            if (!CheckAuth(req, res)) return;
            bool ok = GetKeyStore().Delete(req.matches[1]);
            GetKeyStore().GenerateApiMd(s_service_url);
            res.status = ok ? 204 : 404;
        });

    // POST /api/v1/keys/<id>/rotate
    s_svr->Post(R"(/api/v1/keys/([a-f0-9-]+)/rotate)",
        [](const httplib::Request& req, httplib::Response& res) {
            if (!CheckAuth(req, res)) return;
            std::string newkey = GetKeyStore().Rotate(req.matches[1]);
            GetKeyStore().GenerateApiMd(s_service_url);
            if (newkey.empty()) { res.status = 404; return; }
            json resp;
            resp["key"]    = newkey;
            resp["prefix"] = newkey.substr(0, 12);
            res.set_content(resp.dump(), "application/json");
        });

    // ── Logging Sessions ───────────────────────────────────────────────────────

    // POST /api/v1/sessions
    // Body: {"label":"training-run-001","metric_ids":[],"log_dir":"C:\\MyLogs","duration_sec":3600,
    //        "watch":{"pids":[1234],"exe_names":["python.exe"]},
    //        "stop_policy":{"mode":"process_exit_or_duration"}}
    // log_dir is optional; omit or "" to use the default data_dir\logs\sessions\ location.
    // Response 201: {"session_id":"sess-abc12345","label":"...","started_at_ms":...,"log_path":"...","log_dir":"..."}
    s_svr->Post("/api/v1/sessions", [](const httplib::Request& req, httplib::Response& res) {
        if (!CheckAuth(req, res)) return;
        std::string label;
        std::string log_dir;
        std::vector<uint32_t> metric_ids;
        int64_t duration_ms = 0;
        bool stop_when_watch_empty = false;
        std::vector<uint32_t> watch_pids;
        std::vector<std::string> watch_exes;
        if (!ParseSessionStartBody(req, label, log_dir, metric_ids, duration_ms,
                                   stop_when_watch_empty, watch_pids, watch_exes, res)) return;
        StartSessionFromParsedRequest(label, log_dir, metric_ids, duration_ms,
                                      stop_when_watch_empty, watch_pids, watch_exes, res);
    });

    // GET /api/v1/lab/snapshot
    // Lab-only remote snapshot for explicitly enrolled sensors. This is not the
    // enterprise TLS/mTLS telemetry path.
    s_svr->Get("/api/v1/lab/snapshot", [](const httplib::Request&, httplib::Response& res) {
        if (!LabEnrollmentAllowed(res)) return;
        json snap;
        if (ReadShm(snap))
            res.set_content(snap.dump(), "application/json");
        else
            ErrResp(res, 503, "ERR_SHM_UNAVAILABLE", "Shared memory snapshot unavailable");
    });

    // POST /api/v1/lab/sessions
    // Lab-only remote logging start for explicitly enrolled sensors. The host
    // should omit log_dir unless it knows the remote machine has that path.
    s_svr->Post("/api/v1/lab/sessions", [](const httplib::Request& req, httplib::Response& res) {
        if (!LabEnrollmentAllowed(res)) return;
        std::string label;
        std::string log_dir;
        std::vector<uint32_t> metric_ids;
        int64_t duration_ms = 0;
        bool stop_when_watch_empty = false;
        std::vector<uint32_t> watch_pids;
        std::vector<std::string> watch_exes;
        if (!ParseSessionStartBody(req, label, log_dir, metric_ids, duration_ms,
                                   stop_when_watch_empty, watch_pids, watch_exes, res)) return;
        StartSessionFromParsedRequest(label, log_dir, metric_ids, duration_ms,
                                      stop_when_watch_empty, watch_pids, watch_exes, res);
        if (res.status == 201) DiagnosticLogInfo("Lab remote logging session started by enrolled fleet host.");
    });

    // GET /api/v1/sessions  — list all sessions
    s_svr->Get("/api/v1/sessions", [](const httplib::Request& req, httplib::Response& res) {
        if (!CheckAuth(req, res)) return;
        json arr = json::array();
        for (const auto& s : LogSessionStore::Instance().List()) {
            json j;
            j["session_id"]    = s.id;
            j["label"]         = s.label;
            j["status"]        = (s.status == SessionStatus::Active)  ? "active"
                               : (s.status == SessionStatus::Stopped) ? "stopped" : "failed";
            j["started_at_ms"] = s.started_at_ms;
            j["ended_at_ms"]   = s.ended_at_ms;
            j["duration_ms"]   = s.duration_ms;
            j["stop_when_watch_empty"] = s.stop_when_watch_empty;
            j["rows_written"]  = s.rows_written;
            j["log_path"]      = s.log_path;
            arr.push_back(j);
        }
        res.set_content(arr.dump(), "application/json");
    });

    // GET /api/v1/sessions/{id}  — session detail
    s_svr->Get(R"(/api/v1/sessions/([a-z0-9-]+))",
        [](const httplib::Request& req, httplib::Response& res) {
            if (!CheckAuth(req, res)) return;
            LogSessionStore::SessionSummary s;
            if (!LogSessionStore::Instance().FindSummary(req.matches[1], s)) {
                ErrResp(res, 404, "ERR_SESSION_NOT_FOUND", "Session not found"); return;
            }
            json j;
            j["session_id"]    = s.id;
            j["label"]         = s.label;
            j["status"]        = (s.status == SessionStatus::Active)  ? "active"
                               : (s.status == SessionStatus::Stopped) ? "stopped" : "failed";
            j["started_at_ms"] = s.started_at_ms;
            j["ended_at_ms"]   = s.ended_at_ms;
            j["duration_ms"]   = s.duration_ms;
            j["stop_when_watch_empty"] = s.stop_when_watch_empty;
            j["rows_written"]  = s.rows_written;
            j["log_dir"]       = s.log_dir;
            j["log_path"]      = s.log_path;
            j["metric_ids"]    = s.metric_ids;
            res.set_content(j.dump(), "application/json");
        });

    // POST /api/v1/sessions/{id}/stop
    s_svr->Post(R"(/api/v1/sessions/([a-z0-9-]+)/stop)",
        [](const httplib::Request& req, httplib::Response& res) {
            if (!CheckAuth(req, res)) return;
            std::string log_path = LogSessionStore::Instance().Stop(req.matches[1]);
            if (log_path.empty()) {
                ErrResp(res, 404, "ERR_SESSION_NOT_FOUND",
                        "Session not found or already stopped"); return;
            }
            json resp;
            resp["session_id"] = req.matches[1].str();
            resp["status"]     = "stopped";
            resp["log_path"]   = log_path;
            res.set_content(resp.dump(), "application/json");
        });

    s_svr->Post(R"(/api/v1/lab/sessions/([a-z0-9-]+)/stop)",
        [](const httplib::Request& req, httplib::Response& res) {
            if (!LabEnrollmentAllowed(res)) return;
            std::string log_path = LogSessionStore::Instance().Stop(req.matches[1]);
            if (log_path.empty()) {
                ErrResp(res, 404, "ERR_SESSION_NOT_FOUND",
                        "Lab session not found or already stopped"); return;
            }
            json resp;
            resp["session_id"] = req.matches[1];
            resp["status"]     = "stopped";
            resp["log_path"]   = log_path;
            res.set_content(resp.dump(), "application/json");
            DiagnosticLogInfo("Lab remote logging session stopped by enrolled fleet host.");
        });

    // DELETE /api/v1/sessions/{id}
    s_svr->Delete(R"(/api/v1/sessions/([a-z0-9-]+))",
        [](const httplib::Request& req, httplib::Response& res) {
            if (!CheckAuth(req, res)) return;
            bool ok = LogSessionStore::Instance().Delete(req.matches[1]);
            if (!ok) { ErrResp(res, 404, "ERR_SESSION_NOT_FOUND", "Session not found"); return; }
            res.status = 204;
        });

    // ── Process Watch (runtime config) ─────────────────────────────────────────

    // GET /api/v1/watch  — list watched process config + live entries
    s_svr->Get("/api/v1/watch", [](const httplib::Request& req, httplib::Response& res) {
        if (!CheckAuth(req, res)) return;
        WatchConfig cfg = GetWatcher().GetConfig();
        json j;
        j["exe_names"]   = cfg.exe_names;
        j["pids"]        = cfg.pids;
        json entries = json::array();
        for (const auto& e : GetWatcher().EntriesSnapshot()) {
            json ej;
            ej["exe_name"]        = e.exe_name;
            ej["label"]           = e.label;
            ej["pid"]             = e.pid;
            ej["alive"]           = e.alive;
            ej["cpu_pct"]         = e.cpu_pct;
            ej["mem_private_mb"]  = e.mem_private_mb;
            ej["gpu_sm_pct"]      = e.gpu_sm_pct;
            ej["gpu_vram_mb"]     = e.gpu_vram_mb;
            ej["disk_read_bytes_s"] = e.disk_read_bytes_s;
            ej["disk_write_bytes_s"] = e.disk_write_bytes_s;
            ej["page_faults_s"]   = e.page_faults_per_sec;
            ej["uptime_sec"]      = e.uptime_sec;
            ej["threads"]         = e.thread_count;
            ej["handles"]         = e.handle_count;
            entries.push_back(ej);
        }
        j["live_entries"] = entries;
        res.set_content(j.dump(), "application/json");
    });

    // POST /api/v1/watch  — add process to watch list at runtime
    // Body: {"exe_name":"python.exe","label":"training"} OR {"pid":12345}
    s_svr->Post("/api/v1/watch", [](const httplib::Request& req, httplib::Response& res) {
        if (!CheckAuth(req, res)) return;
        try {
            json body = json::parse(req.body);
            if (body.contains("exe_name")) {
                std::string name  = body["exe_name"].get<std::string>();
                std::string label = body.value("label", "");
                if (!GetWatcher().AddWatch(name, label)) {
                    ErrResp(res, 409, "ERR_ALREADY_WATCHED",
                            "That exe_name is already in the watch list"); return;
                }
                json resp;
                resp["exe_name"] = name;
                resp["label"]    = label.empty() ? name : label;
                resp["status"]   = "added";
                res.status = 201;
                res.set_content(resp.dump(), "application/json");
            } else if (body.contains("pid")) {
                uint32_t pid = body["pid"].get<uint32_t>();
                if (!GetWatcher().AddPid(pid)) {
                    ErrResp(res, 409, "ERR_ALREADY_WATCHED",
                            "That pid is already in the watch list"); return;
                }
                json resp;
                resp["pid"]    = pid;
                resp["status"] = "added";
                res.status = 201;
                res.set_content(resp.dump(), "application/json");
            } else {
                ErrResp(res, 400, "ERR_MISSING_FIELD",
                        "Body must contain 'exe_name' or 'pid'");
            }
        } catch (...) {
            ErrResp(res, 400, "ERR_INVALID_BODY", "Request body must be valid JSON");
        }
    });

    // DELETE /api/v1/watch/{exe_name}  — remove from watch list
    s_svr->Delete(R"(/api/v1/watch/(.+))",
        [](const httplib::Request& req, httplib::Response& res) {
            if (!CheckAuth(req, res)) return;
            std::string target = req.matches[1];
            char* end = nullptr;
            unsigned long pid = std::strtoul(target.c_str(), &end, 10);
            bool ok = (end && *end == '\0')
                ? GetWatcher().RemovePid(static_cast<uint32_t>(pid))
                : GetWatcher().RemoveWatch(target);
            if (!ok) {
                ErrResp(res, 404, "ERR_NOT_WATCHED",
                        "That exe_name or pid is not in the watch list"); return;
            }
            res.status = 204;
        });

    DiagnosticLogInfo("HttpServerInit complete; routes registered.");
    return true;
}

void HttpServerRun(std::atomic<bool>& stop) {
    if (!s_svr) return;
    const char* bind_addr = HttpBindAddress();
    DiagnosticLogInfo("HTTP server listening on " + std::string(bind_addr) + ":" +
                      std::to_string(HTTP_PORT));
    if (std::string(bind_addr) == "0.0.0.0") {
        DiagnosticLogWarn("Remote API binding enabled. Use only with firewall controls, API keys, and planned TLS/mTLS hardening.");
    }
    // cpp-httplib blocks in listen(); we poll stop on a timer
    std::thread stopper([&]{ while (!stop.load()) Sleep(200); s_svr->stop(); });
    s_svr->listen(bind_addr, HTTP_PORT);
    stopper.join();
    DiagnosticLogInfo("HTTP server stopped.");
}

void HttpServerShutdown() {
    DiagnosticLogInfo("HttpServerShutdown begin.");
    if (s_svr) { s_svr->stop(); delete s_svr; s_svr = nullptr; }
    DiagnosticLogInfo("HttpServerShutdown complete.");
}

} // namespace Service
