#pragma once
#include <cstdint>
#include <vector>
#include <string>

namespace Sensors {

enum class GpuVendor { NVIDIA, AMD, INTEL, UNKNOWN };

struct GpuSnapshot {
    int         index;
    std::string name;
    GpuVendor   vendor;
    std::string vendor_str;

    // Common telemetry (set by all vendor paths or left 0)
    double usage_pct;
    double vram_used_mb;
    double vram_total_mb;
    double vram_percent;
    double temp_celsius;
    double power_watts;
    double fan_pct;
    double clock_core_mhz;
    double clock_mem_mhz;
    double encoder_pct;
    double decoder_pct;
    double pdh_3d_pct;          // PDH GPU Engine 3D aggregate
    double pdh_compute_pct;     // PDH GPU Engine Compute aggregate

    // NVIDIA-only
    bool   throttle_active;
    std::vector<std::string> throttle_reasons;
    double thermal_efficiency;  // usage_pct / temp_celsius
    double sm_util_pct;         // nvmlDeviceGetUtilizationRates (SM)
    double mem_bw_util_pct;
    int    mem_clk_transitions;
    int    cuda_cc_major;
    int    cuda_cc_minor;
    bool   has_tensor_cores;
    int    tensor_core_gen;

    GpuSnapshot() : index(0), vendor(GpuVendor::UNKNOWN), usage_pct(0),
        vram_used_mb(0), vram_total_mb(0), vram_percent(0), temp_celsius(0),
        power_watts(0), fan_pct(0), clock_core_mhz(0), clock_mem_mhz(0),
        encoder_pct(0), decoder_pct(0), pdh_3d_pct(0), pdh_compute_pct(0),
        throttle_active(false), thermal_efficiency(0), sm_util_pct(0),
        mem_bw_util_pct(0), mem_clk_transitions(0), cuda_cc_major(0),
        cuda_cc_minor(0), has_tensor_cores(false), tensor_core_gen(0) {}
};

// Per-process GPU data
struct ProcGpuData {
    uint32_t pid;
    uint64_t vram_bytes;
    uint32_t sm_util;
    uint32_t mem_util;
    uint32_t enc_util;
    uint32_t dec_util;
};

// Unified GPU subsystem — probes vendor DLLs at init, selects best path per adapter
bool GpuInit();
void GpuPoll(std::vector<GpuSnapshot>& snaps);
void GpuPollPerProcess(const std::vector<uint32_t>& pids, std::vector<ProcGpuData>& out);
void GpuShutdown();
int  GpuCount();

} // namespace Sensors
