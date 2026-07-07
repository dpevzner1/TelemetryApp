#pragma once
#include <windows.h>
#include <pdh.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "pdh.lib")

namespace Sensors {

// GPU Engine PDH data for one physical GPU
struct PdhGpuResult {
    double phys_3d_pct;      // 3D engine aggregate (matches Task Manager)
    double phys_compute_pct; // Compute engine aggregate
};

// Per-PID PDH GPU Engine utilization (3D + Compute combined)
struct PidGpuUtil {
    uint32_t pid;
    double   util_pct;
};

// Persistent PDH query set — opened once at init, collected each poll.
// A single 50ms sleep covers all counters (no duplicate sleeps).
struct PdhQueries {
    HQUERY  query          = nullptr;
    HCOUNTER gpu_engine    = nullptr;  // GPU Engine(*)\Utilization Percentage wildcard
    bool    ready          = false;

    // Collect into results. Call once per poll after the 50ms stabilisation sleep.
    // phys_out: indexed by physical GPU index
    // pid_out:  per-process util (only for PIDs in target_pids)
    void Collect(
        std::unordered_map<uint32_t, PdhGpuResult>& phys_out,
        std::vector<PidGpuUtil>&                     pid_out,
        const std::vector<uint32_t>&                 target_pids);
};

bool PdhInit(PdhQueries& q);
void PdhCollect(PdhQueries& q);   // single CollectQueryData (call at poll start + after 50ms)
void PdhShutdown(PdhQueries& q);

} // namespace Sensors
