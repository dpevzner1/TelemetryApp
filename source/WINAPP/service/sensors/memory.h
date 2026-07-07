#pragma once
#include <cstdint>

namespace Sensors {

struct MemSnapshot {
    double total_gb;
    double used_gb;
    double available_gb;
    double percent;
    double swap_total_gb;
    double swap_used_gb;
    double swap_percent;
    double standby_gb;      // NtQuerySystemInformation SystemMemoryListInformation
    double modified_gb;
    double commit_total_gb;
    double commit_used_gb;
    double page_fault_rate; // faults/sec (from perf counters delta)
};

bool MemInit();
bool MemPoll(MemSnapshot& snap, double elapsed_sec);
void MemShutdown();

} // namespace Sensors
