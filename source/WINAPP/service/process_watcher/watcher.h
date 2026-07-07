#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <cstdint>

namespace Service {

struct WatchEntry {
    std::string   exe_name;   // e.g. "python.exe" (case-insensitive match)
    std::string   label;      // user-visible label
    uint32_t      pid;
    uint64_t      create_time_100ns;  // guards against PID reuse
    bool          alive;

    // Per-poll metrics
    double  cpu_pct;
    double  mem_private_mb;
    double  mem_peak_wset_mb;
    double  page_faults_per_sec;
    double  ctx_switches_per_sec;
    double  gpu_sm_pct;
    double  gpu_vram_mb;
    double  gpu_mem_util_pct;
    double  disk_read_bytes_s;
    double  disk_write_bytes_s;
    double  net_recv_bytes_s;
    double  net_sent_bytes_s;
    double  uptime_sec;
    int     thread_count;
    int     handle_count;
    int     cpu_affinity_mask;
    int     priority_class;

    // Prev-tick accumulators for delta calculations
    uint64_t prev_kernel_time;
    uint64_t prev_user_time;
    uint64_t prev_io_read_bytes;
    uint64_t prev_io_write_bytes;
    int64_t  prev_page_faults;
    int64_t  prev_ctx_switches;
};

struct WatchConfig {
    std::vector<std::string> exe_names; // processes to watch (by exe name)
    std::vector<uint32_t>    pids;      // explicit PIDs (union with exe_names)
};

class ProcessWatcher {
public:
    ProcessWatcher() = default;

    // Replace entire config atomically (called from HTTP or startup)
    void SetConfig(const WatchConfig& cfg);
    WatchConfig GetConfig() const;

    // Runtime add/remove — no service restart required
    // Returns false if exe_name already tracked / not found
    bool AddWatch(const std::string& exe_name, const std::string& label = "");
    bool RemoveWatch(const std::string& exe_name);
    bool AddPid(uint32_t pid);
    bool RemovePid(uint32_t pid);

    // Called once per poll cycle with elapsed time since last poll
    void Poll(double elapsed_sec);

    // Snapshot of live entries — returns a copy so callers need no lock
    std::vector<WatchEntry> EntriesSnapshot() const;

    // All watched PIDs (for PDH GPU per-process query)
    std::vector<uint32_t> GetWatchedPids() const;

    // Write live entries into SHM WATCH_BASE slots
    void WriteToShm() const;

private:
    mutable std::mutex      m_mu;
    WatchConfig             m_cfg;
    std::vector<WatchEntry> m_entries;
    std::unordered_map<std::string, std::string> m_pending_labels; // exe→label

    // Private helpers — callers must hold m_mu
    void DiscoverNewProcesses();
    void UpdateEntry(WatchEntry& e, double elapsed_sec);
    void RemoveDeadEntries();
    std::vector<uint32_t> GetWatchedPidsLocked() const;
};

// Singleton accessor used by poll_loop and http_server
ProcessWatcher& GetWatcher();

} // namespace Service
