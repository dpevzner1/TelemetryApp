#pragma once
#include <string>
#include <vector>

namespace Service {

struct ValidationResult {
    std::string check_id;
    bool        passed;
    std::string detail;
};

// VC-01: CPU usage in [0,100]
// VC-02: Memory percent in (0,100]
// VC-03: GPU usage in [0,100] for each adapter
// VC-04: Disk busy_pct in [0,100]
// VC-05: Network bytes/s >= 0
// VC-06: SHM seqlock is even (stable) before publish
// VC-07: Per-core CPU sum within ±N% of total
// VC-08: Poll duration < 200ms (service not falling behind)
// VC-09: Temperature readings in [0, 150] °C
// VC-10: Self-monitoring CPU < 5% (service overhead guard)

struct ValidationInput {
    double  cpu_total_pct;
    double  mem_pct;
    double* gpu_pct;       // array
    int     gpu_count;
    double* disk_busy;     // array
    int     disk_count;
    bool    shm_seq_even;
    double* per_core_pct;
    int     core_count;
    double  poll_ms;
    double* temp_readings; // array
    int     temp_count;
    double  self_cpu_pct;
};

std::vector<ValidationResult> RunValidations(const ValidationInput& in);
// Logs any failed checks via Service::LogEvent
void LogFailedValidations(const std::vector<ValidationResult>& results);

} // namespace Service
