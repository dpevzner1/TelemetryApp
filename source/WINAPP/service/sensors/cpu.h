#pragma once
#include <cstdint>
#include <array>

namespace Sensors {

struct CoreSample {
    int64_t kernel;   // LARGE_INTEGER.QuadPart
    int64_t user;
    int64_t idle;
};

struct CpuSnapshot {
    double   usage_total_pct;
    double   freq_actual_mhz;
    double   freq_rated_max_mhz;
    double   core_balance_score;    // 0.0–1.0 (1.0 = perfectly even)
    int      hot_core_index;
    double   hot_core_pct;
    int      cold_cores_count;
    int64_t  ctx_switches_total;
    int64_t  interrupts_total;
    int      core_count;
    double   per_core_pct[32];
};

// Call once at startup. Loads ntdll!NtQuerySystemInformation via GetProcAddress,
// seeds prev samples, and queries rated max frequency from registry.
bool CpuInit();

// Call each poll cycle. Fills snap and returns true on success.
bool CpuPoll(CpuSnapshot& snap);

void CpuShutdown();

} // namespace Sensors
