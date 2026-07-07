#pragma once
#include <cstdint>

// Stable numeric IDs — indices into the SHM MetricRing[METRIC_COUNT] array.
// Never reorder, never remove. Append-only. Max = METRIC_COUNT (512).
//
// Indexed subsystems (GPU, disk, NIC, watched-process) use base + stride helpers
// defined at the bottom. Per-instance offsets live in GpuOff/DiskOff/NetOff/WatchOff.

enum MetricId : uint32_t {
    // ── CPU (0–63) ───────────────────────────────────────────────────────────
    CPU_USAGE_TOTAL          =  0,
    CPU_FREQ_ACTUAL_MHZ      =  1,
    CPU_FREQ_RATED_MAX_MHZ   =  2,
    CPU_CORE_BALANCE_SCORE   =  3,
    CPU_HOT_CORE_INDEX       =  4,
    CPU_HOT_CORE_PCT         =  5,
    CPU_COLD_CORES_COUNT     =  6,
    CPU_CTX_SWITCHES_TOTAL   =  7,
    CPU_INTERRUPTS_TOTAL     =  8,
    CPU_PACKAGE_TEMP_C       =  9,
    CPU_PACKAGE_POWER_W      = 10,
    // 11–15 reserved
    CPU_CORE_BASE            = 16,  // +core_idx (0–31) = per-core usage %
    CPU_CORE_MAX             = 47,
    // 48–63 reserved

    // ── Memory (64–95) ───────────────────────────────────────────────────────
    MEM_TOTAL_GB             = 64,
    MEM_USED_GB              = 65,
    MEM_AVAILABLE_GB         = 66,
    MEM_PERCENT              = 67,
    MEM_SWAP_TOTAL_GB        = 68,
    MEM_SWAP_USED_GB         = 69,
    MEM_SWAP_PERCENT         = 70,
    MEM_STANDBY_GB           = 71,   // NtQuerySystemInformation SystemMemoryListInformation
    MEM_MODIFIED_GB          = 72,
    MEM_COMMIT_TOTAL_GB      = 73,
    MEM_COMMIT_USED_GB       = 74,
    MEM_PAGE_FAULT_RATE      = 75,
    // 76–95 reserved

    // ── GPU (96–223) — 4 GPUs × 32 metrics each ──────────────────────────────
    // Access via: gpu_metric(gpu_idx, GpuOff::*)
    GPU_BASE                 = 96,
    GPU_STRIDE               = 32,
    GPU_MAX_COUNT            =  4,
    // 96–223 reserved for GPU

    // ── Disk (224–287) — 8 devices × 8 metrics each ──────────────────────────
    // Access via: disk_metric(dev_idx, DiskOff::*)
    DISK_BASE                = 224,
    DISK_STRIDE              =   8,
    DISK_MAX_COUNT           =   8,

    // ── Network (288–351) — 8 NICs × 8 metrics each ──────────────────────────
    // Access via: net_metric(nic_idx, NetOff::*)
    NET_BASE                 = 288,
    NET_STRIDE               =   8,
    NET_MAX_COUNT            =   8,

    // ── Temperatures (352–383) — up to 32 sensors ────────────────────────────
    // Access via: temp_metric(sensor_idx)
    TEMP_BASE                = 352,
    TEMP_MAX_COUNT           =  32,

    // ── Self-overhead (384–399) ───────────────────────────────────────────────
    SELF_CPU_PCT             = 384,
    SELF_RSS_MB              = 385,
    SELF_PRIVATE_MB          = 386,
    SELF_HANDLES             = 387,
    SELF_POLL_MS             = 388,
    SELF_DISK_READ_BYTES_S   = 389,
    SELF_DISK_WRITE_BYTES_S  = 390,
    SELF_THREADS             = 391,
    SELF_MEM_PCT             = 392,
    // 393–399 reserved

    // ── Watched processes (400–511) — 8 slots × 14 metrics each ─────────────
    // Access via: watch_metric(slot_idx, WatchOff::*)
    WATCH_BASE               = 400,
    WATCH_STRIDE             =  14,
    WATCH_MAX_COUNT          =   8,

    METRIC_COUNT             = 512,
};

// ── Per-GPU metric offsets ────────────────────────────────────────────────────
namespace GpuOff {
    enum : uint32_t {
        USAGE_PCT           =  0,
        VRAM_USED_MB        =  1,
        VRAM_TOTAL_MB       =  2,
        TEMP_C              =  3,
        POWER_W             =  4,
        FAN_PCT             =  5,
        CLOCK_CORE_MHZ      =  6,
        CLOCK_MEM_MHZ       =  7,
        THROTTLE_ACTIVE     =  8,
        THERMAL_EFFICIENCY  =  9,
        ENCODER_PCT         = 10,
        DECODER_PCT         = 11,
        SM_UTIL_PCT         = 12,  // NVML nvmlDeviceGetProcessUtilization
        MEM_BW_UTIL_PCT     = 13,
        PDH_UTIL_PCT        = 14,  // PDH GPU Engine (3D+Compute)
        MEM_CLK_TRANSITIONS = 15,
        TENSOR_ACTIVE       = 16,  // estimated tensor core active flag
        VRAM_PCT            = 17,  // vram_used / vram_total * 100
        CUDA_CC_MAJOR       = 18,
        CUDA_CC_MINOR       = 19,
        TENSOR_CORE_GEN     = 20,
    };
}

// ── Per-disk metric offsets ───────────────────────────────────────────────────
namespace DiskOff {
    enum : uint32_t {
        READ_BYTES_S  = 0,
        WRITE_BYTES_S = 1,
        READ_IOPS     = 2,
        WRITE_IOPS    = 3,
        BUSY_PCT      = 4,
        FREE_GB       = 5,
        USED_PCT      = 6,
    };
}

// ── Per-NIC metric offsets ────────────────────────────────────────────────────
namespace NetOff {
    enum : uint32_t {
        RECV_BYTES_S  = 0,
        SENT_BYTES_S  = 1,
        RECV_PKTS_S   = 2,
        SENT_PKTS_S   = 3,
        ERRORS_IN     = 4,
        ERRORS_OUT    = 5,
        DROPS_IN      = 6,
        DROPS_OUT     = 7,
    };
}

// ── Per-watched-process metric offsets ───────────────────────────────────────
namespace WatchOff {
    enum : uint32_t {
        CPU_PCT           =  0,
        RSS_MB            =  1,
        PRIVATE_MB        =  2,
        MEM_PCT           =  3,
        THREADS           =  4,
        HANDLES           =  5,
        DISK_READ_BYTES_S =  6,
        DISK_WRITE_BYTES_S=  7,
        GPU_VRAM_MB       =  8,
        GPU_SM_PCT        =  9,
        GPU_PDH_PCT       = 10,
        UPTIME_S          = 11,
        PAGE_FAULTS_S     = 12,
        CTX_SWITCHES_S    = 13,
    };
}

// ── Index computation helpers ─────────────────────────────────────────────────
inline constexpr uint32_t gpu_metric(uint32_t gpu_idx, uint32_t off) noexcept {
    return MetricId::GPU_BASE + gpu_idx * MetricId::GPU_STRIDE + off;
}
inline constexpr uint32_t disk_metric(uint32_t dev_idx, uint32_t off) noexcept {
    return MetricId::DISK_BASE + dev_idx * MetricId::DISK_STRIDE + off;
}
inline constexpr uint32_t net_metric(uint32_t nic_idx, uint32_t off) noexcept {
    return MetricId::NET_BASE + nic_idx * MetricId::NET_STRIDE + off;
}
inline constexpr uint32_t temp_metric(uint32_t sensor_idx) noexcept {
    return MetricId::TEMP_BASE + sensor_idx;
}
inline constexpr uint32_t watch_metric(uint32_t slot, uint32_t off) noexcept {
    return MetricId::WATCH_BASE + slot * MetricId::WATCH_STRIDE + off;
}
inline constexpr uint32_t cpu_core_metric(uint32_t core_idx) noexcept {
    return MetricId::CPU_CORE_BASE + core_idx;
}
