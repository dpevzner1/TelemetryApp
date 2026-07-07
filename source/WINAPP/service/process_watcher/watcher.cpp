#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include "watcher.h"
#include "../ipc/shm_writer.h"
#include "../sensors/gpu_nvml.h"
#include "../../shared/shm_layout.h"
#include "../../shared/metric_ids.h"

#pragma comment(lib, "psapi.lib")

namespace Service {

static ProcessWatcher s_watcher;
ProcessWatcher& GetWatcher() { return s_watcher; }

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::string ToLower(std::string s) {
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static std::vector<std::pair<std::string, uint32_t>> SnapshotProcesses() {
    std::vector<std::pair<std::string, uint32_t>> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W pe{ sizeof(pe) };
    if (Process32FirstW(snap, &pe)) {
        do {
            char name[MAX_PATH]{};
            WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1,
                                name, MAX_PATH, nullptr, nullptr);
            out.push_back({ToLower(name), pe.th32ProcessID});
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return out;
}

static uint64_t GetProcessCreateTime(HANDLE h) {
    FILETIME ct, et, kt, ut;
    if (!GetProcessTimes(h, &ct, &et, &kt, &ut)) return 0;
    ULARGE_INTEGER u{ ct.dwLowDateTime, ct.dwHighDateTime };
    return u.QuadPart;
}

// ── Config — thread-safe ──────────────────────────────────────────────────────

static int CountThreads(uint32_t pid) {
    int count = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 te{ sizeof(te) };
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID == pid) ++count;
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);
    return count;
}

void ProcessWatcher::SetConfig(const WatchConfig& cfg) {
    std::lock_guard<std::mutex> lk(m_mu);
    m_cfg = cfg;
    for (auto& n : m_cfg.exe_names) n = ToLower(n);
}

WatchConfig ProcessWatcher::GetConfig() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return m_cfg;
}

bool ProcessWatcher::AddWatch(const std::string& exe_name, const std::string& label) {
    std::lock_guard<std::mutex> lk(m_mu);
    std::string low = ToLower(exe_name);
    // Reject duplicates
    for (const auto& n : m_cfg.exe_names)
        if (n == low) return false;
    m_cfg.exe_names.push_back(low);
    // If caller provided a label, store it in a pending-label map would be ideal;
    // for now the label is populated when DiscoverNewProcesses() creates the entry.
    // We store it separately so discovery can pick it up.
    m_pending_labels[low] = label.empty() ? low : label;
    return true;
}

bool ProcessWatcher::RemoveWatch(const std::string& exe_name) {
    std::lock_guard<std::mutex> lk(m_mu);
    std::string low = ToLower(exe_name);
    auto& names = m_cfg.exe_names;
    auto it = std::find(names.begin(), names.end(), low);
    if (it == names.end()) return false;
    names.erase(it);
    for (auto& e : m_entries)
        if (e.exe_name == low) e.alive = false;
    m_pending_labels.erase(low);
    return true;
}

bool ProcessWatcher::AddPid(uint32_t pid) {
    std::lock_guard<std::mutex> lk(m_mu);
    if (std::find(m_cfg.pids.begin(), m_cfg.pids.end(), pid) != m_cfg.pids.end())
        return false;
    m_cfg.pids.push_back(pid);
    return true;
}

bool ProcessWatcher::RemovePid(uint32_t pid) {
    std::lock_guard<std::mutex> lk(m_mu);
    auto it = std::find(m_cfg.pids.begin(), m_cfg.pids.end(), pid);
    if (it == m_cfg.pids.end()) return false;
    m_cfg.pids.erase(it);
    for (auto& e : m_entries)
        if (e.pid == pid) e.alive = false;
    return true;
}

// ── EntriesSnapshot ───────────────────────────────────────────────────────────

std::vector<WatchEntry> ProcessWatcher::EntriesSnapshot() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return m_entries;
}

// ── Discovery / Update (called with lock held) ────────────────────────────────

void ProcessWatcher::DiscoverNewProcesses() {
    // Lock is already held by Poll()
    auto running = SnapshotProcesses();

    auto already_tracked = [&](uint32_t pid) {
        for (const auto& e : m_entries) if (e.pid == pid) return true;
        return false;
    };

    // Discover by exe name
    for (const auto& [name, pid] : running) {
        for (const auto& watch_name : m_cfg.exe_names) {
            if (name != watch_name) continue;
            if (already_tracked(pid)) break;

            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
                                   FALSE, pid);
            if (!h) break;
            WatchEntry e{};
            e.exe_name           = name;
            auto lit = m_pending_labels.find(name);
            e.label              = (lit != m_pending_labels.end()) ? lit->second : name;
            e.pid                = pid;
            e.create_time_100ns  = GetProcessCreateTime(h);
            e.alive              = true;
            CloseHandle(h);
            m_entries.push_back(e);
            break;
        }
    }

    // Discover by explicit PID
    for (uint32_t pid : m_cfg.pids) {
        if (already_tracked(pid)) continue;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!h) continue;
        WatchEntry e{};
        e.pid                = pid;
        e.create_time_100ns  = GetProcessCreateTime(h);
        e.alive              = true;
        wchar_t path[MAX_PATH]{};
        DWORD sz = MAX_PATH;
        if (QueryFullProcessImageNameW(h, 0, path, &sz)) {
            wchar_t* last = wcsrchr(path, L'\\');
            char name_a[MAX_PATH]{};
            WideCharToMultiByte(CP_UTF8, 0, last ? last + 1 : path, -1,
                                name_a, MAX_PATH, nullptr, nullptr);
            e.exe_name = ToLower(name_a);
            e.label    = e.exe_name;
        }
        CloseHandle(h);
        m_entries.push_back(e);
    }
}

void ProcessWatcher::UpdateEntry(WatchEntry& e, double elapsed_sec) {
    // Lock already held by Poll()
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
                           FALSE, e.pid);
    if (!h) { e.alive = false; return; }

    // PID reuse guard
    if (GetProcessCreateTime(h) != e.create_time_100ns) {
        e.alive = false; CloseHandle(h); return;
    }

    // CPU
    FILETIME ct_ft, et_ft, kt_ft, ut_ft;
    if (GetProcessTimes(h, &ct_ft, &et_ft, &kt_ft, &ut_ft)) {
        ULARGE_INTEGER kt{ kt_ft.dwLowDateTime, kt_ft.dwHighDateTime };
        ULARGE_INTEGER ut{ ut_ft.dwLowDateTime, ut_ft.dwHighDateTime };
        uint64_t total = kt.QuadPart + ut.QuadPart;
        uint64_t prev  = e.prev_kernel_time + e.prev_user_time;
        SYSTEM_INFO si{}; GetSystemInfo(&si);
        double avail = elapsed_sec * 1e7 * si.dwNumberOfProcessors;
        e.cpu_pct          = avail > 0
            ? std::min((total - prev) / avail * 100.0, 100.0 * si.dwNumberOfProcessors)
            : 0.0;
        e.prev_kernel_time = kt.QuadPart;
        e.prev_user_time   = ut.QuadPart;

        ULARGE_INTEGER ctu{ ct_ft.dwLowDateTime, ct_ft.dwHighDateTime };
        FILETIME now_ft; GetSystemTimeAsFileTime(&now_ft);
        ULARGE_INTEGER now_ui{ now_ft.dwLowDateTime, now_ft.dwHighDateTime };
        e.uptime_sec = static_cast<double>(now_ui.QuadPart - ctu.QuadPart) / 1e7;
    }

    // Memory
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(h, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
        e.mem_private_mb   = static_cast<double>(pmc.PrivateUsage)      / (1024.0 * 1024.0);
        e.mem_peak_wset_mb = static_cast<double>(pmc.PeakWorkingSetSize) / (1024.0 * 1024.0);
        int64_t pf_now = static_cast<int64_t>(pmc.PageFaultCount);
        if (e.prev_page_faults > 0 && elapsed_sec > 0)
            e.page_faults_per_sec = (pf_now - e.prev_page_faults) / elapsed_sec;
        e.prev_page_faults = pf_now;
    }

    // I/O
    IO_COUNTERS io{};
    if (GetProcessIoCounters(h, &io)) {
        if (e.prev_io_read_bytes > 0 && elapsed_sec > 0) {
            e.disk_read_bytes_s  = static_cast<double>(io.ReadTransferCount  - e.prev_io_read_bytes)  / elapsed_sec;
            e.disk_write_bytes_s = static_cast<double>(io.WriteTransferCount - e.prev_io_write_bytes) / elapsed_sec;
        }
        e.prev_io_read_bytes  = io.ReadTransferCount;
        e.prev_io_write_bytes = io.WriteTransferCount;
    }

    // Priority + affinity
    e.priority_class = static_cast<int>(GetPriorityClass(h));
    DWORD handle_count = 0;
    if (GetProcessHandleCount(h, &handle_count))
        e.handle_count = static_cast<int>(handle_count);
    e.thread_count = CountThreads(e.pid);
    DWORD_PTR proc_mask = 0, sys_mask = 0;
    GetProcessAffinityMask(h, &proc_mask, &sys_mask);
    e.cpu_affinity_mask = static_cast<int>(proc_mask);

    CloseHandle(h);
}

void ProcessWatcher::RemoveDeadEntries() {
    // Lock already held by Poll()
    m_entries.erase(
        std::remove_if(m_entries.begin(), m_entries.end(),
                       [](const WatchEntry& e){ return !e.alive; }),
        m_entries.end());
}

// ── Poll ──────────────────────────────────────────────────────────────────────

void ProcessWatcher::Poll(double elapsed_sec) {
    std::lock_guard<std::mutex> lk(m_mu);
    DiscoverNewProcesses();
    for (auto& e : m_entries) UpdateEntry(e, elapsed_sec);

    // NVML per-process GPU (lock is held but NVML calls are fast)
    auto pids = GetWatchedPidsLocked();
    if (!pids.empty() && Sensors::NvmlAvailable()) {
        std::vector<Sensors::ProcGpuData> gpu_data;
        Sensors::NvmlPollPerProcess(pids, gpu_data);
        for (auto& e : m_entries) {
            for (const auto& g : gpu_data) {
                if (g.pid != e.pid) continue;
                e.gpu_vram_mb      = static_cast<double>(g.vram_bytes) / (1024.0 * 1024.0);
                e.gpu_sm_pct       = g.sm_util;
                e.gpu_mem_util_pct = g.mem_util;
                break;
            }
        }
    }

    RemoveDeadEntries();
}

std::vector<uint32_t> ProcessWatcher::GetWatchedPidsLocked() const {
    // Called from Poll() with lock already held
    std::vector<uint32_t> out;
    for (const auto& e : m_entries) if (e.alive) out.push_back(e.pid);
    return out;
}

std::vector<uint32_t> ProcessWatcher::GetWatchedPids() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return GetWatchedPidsLocked();
}

// ── WriteToShm ────────────────────────────────────────────────────────────────

void ProcessWatcher::WriteToShm() const {
    std::lock_guard<std::mutex> lk(m_mu);
    ShmBlock* shm = ShmGet();
    if (!shm) return;

    int slot = 0;
    for (const auto& e : m_entries) {
        if (!e.alive || slot >= 8) break;
        ShmPush(shm, watch_metric(slot, WatchOff::CPU_PCT),           e.cpu_pct);
        ShmPush(shm, watch_metric(slot, WatchOff::PRIVATE_MB),        e.mem_private_mb);
        ShmPush(shm, watch_metric(slot, WatchOff::GPU_SM_PCT),        e.gpu_sm_pct);
        ShmPush(shm, watch_metric(slot, WatchOff::GPU_VRAM_MB),       e.gpu_vram_mb);
        ShmPush(shm, watch_metric(slot, WatchOff::DISK_READ_BYTES_S), e.disk_read_bytes_s);
        ShmPush(shm, watch_metric(slot, WatchOff::DISK_WRITE_BYTES_S),e.disk_write_bytes_s);
        ShmPush(shm, watch_metric(slot, WatchOff::PAGE_FAULTS_S),     e.page_faults_per_sec);
        ShmPush(shm, watch_metric(slot, WatchOff::UPTIME_S),          e.uptime_sec);
        ShmPush(shm, watch_metric(slot, WatchOff::THREADS),           e.thread_count);
        ShmPush(shm, watch_metric(slot, WatchOff::HANDLES),           e.handle_count);
        strncpy(shm->hdr.watch_label[slot], e.label.c_str(), SHM_WATCH_LABEL_LEN - 1);
        ++slot;
    }
    shm->hdr.active_watch_count = static_cast<uint32_t>(slot);
}

} // namespace Service
