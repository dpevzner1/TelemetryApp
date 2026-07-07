#pragma once
// JSON field name constants shared between http_server.cpp and any client
// that parses the REST API. Centralising here prevents typo-drift.

namespace ApiField {
    // Snapshot root keys
    constexpr const char* TIMESTAMP    = "timestamp";
    constexpr const char* CPU          = "cpu";
    constexpr const char* MEMORY       = "memory";
    constexpr const char* GPUS         = "gpus";
    constexpr const char* PDH_GPU_UTIL = "pdh_gpu_util";
    constexpr const char* DISK         = "disk";
    constexpr const char* NETWORK      = "network";
    constexpr const char* TEMPERATURES = "temperatures";
    constexpr const char* POWER        = "power";
    constexpr const char* SELF_OVERHEAD= "self_overhead";
    constexpr const char* WATCHED      = "watched_processes";
    constexpr const char* ADJUSTED     = "adjusted";

    // CPU sub-keys
    constexpr const char* USAGE_PERCENT      = "usage_percent";
    constexpr const char* PER_CORE_PERCENT   = "per_core_percent";
    constexpr const char* FREQ_ACTUAL_MHZ    = "frequency_mhz_actual";
    constexpr const char* FREQ_RATED_MAX_MHZ = "frequency_mhz_rated_max";
    constexpr const char* FREQ_SOURCE        = "frequency_source";
    constexpr const char* CORE_BALANCE       = "core_balance";

    // GPU sub-keys
    constexpr const char* INDEX             = "index";
    constexpr const char* NAME              = "name";
    constexpr const char* MEM_USED_MB       = "memory_used_mb";
    constexpr const char* MEM_TOTAL_MB      = "memory_total_mb";
    constexpr const char* MEM_PERCENT       = "memory_percent";
    constexpr const char* TEMP_CELSIUS      = "temperature_celsius";
    constexpr const char* POWER_WATTS       = "power_watts";
    constexpr const char* FAN_PERCENT       = "fan_percent";
    constexpr const char* CLOCK_CORE_MHZ    = "clock_core_mhz";
    constexpr const char* CLOCK_MEM_MHZ     = "clock_memory_mhz";
    constexpr const char* THROTTLE_ACTIVE   = "throttle_active";
    constexpr const char* THROTTLE_REASONS  = "throttle_reasons";
    constexpr const char* THERMAL_EFF       = "thermal_efficiency";
    constexpr const char* ENCODER_PCT       = "encoder_percent";
    constexpr const char* DECODER_PCT       = "decoder_percent";
    constexpr const char* SM_UTIL_PCT       = "sm_util_percent";
    constexpr const char* MEM_BW_UTIL_PCT   = "mem_bw_util_percent";
    constexpr const char* CUDA_CC           = "cuda_compute_capability";
    constexpr const char* TENSOR_CORES      = "tensor_cores";
    constexpr const char* TENSOR_CORE_GEN   = "tensor_core_generation";
    constexpr const char* VENDOR            = "vendor";

    // Watch keys
    constexpr const char* WATCH_ID          = "watch_id";
    constexpr const char* LABEL             = "label";
    constexpr const char* TARGET            = "target";
    constexpr const char* ALIVE             = "alive";
    constexpr const char* PID              = "pid";
    constexpr const char* PROCESSES        = "processes";

    // Auth
    constexpr const char* API_KEY_HEADER   = "X-API-Key";
    constexpr const char* API_KEY_PARAM    = "api_key";
}

// Prometheus metric name constants
namespace PromName {
    constexpr const char* CPU_USAGE        = "sys_cpu_usage_percent";
    constexpr const char* CPU_CORE_USAGE   = "sys_cpu_core_usage_percent";
    constexpr const char* CPU_FREQ_MHZ     = "sys_cpu_frequency_mhz";
    constexpr const char* MEM_TOTAL        = "sys_memory_total_bytes";
    constexpr const char* MEM_USED         = "sys_memory_used_bytes";
    constexpr const char* MEM_AVAIL        = "sys_memory_available_bytes";
    constexpr const char* MEM_PCT          = "sys_memory_usage_percent";
    constexpr const char* GPU_USAGE        = "sys_gpu_usage_percent";
    constexpr const char* GPU_MEM_USED     = "sys_gpu_memory_used_mb";
    constexpr const char* GPU_MEM_TOTAL    = "sys_gpu_memory_total_mb";
    constexpr const char* GPU_TEMP         = "sys_gpu_temperature_celsius";
    constexpr const char* GPU_POWER        = "sys_gpu_power_watts";
    constexpr const char* GPU_THROTTLE     = "sys_gpu_throttle_active";
    constexpr const char* GPU_PDH_USAGE    = "sys_gpu_pdh_usage_percent";
    constexpr const char* DISK_READ        = "sys_disk_read_bytes_per_sec";
    constexpr const char* DISK_WRITE       = "sys_disk_write_bytes_per_sec";
    constexpr const char* DISK_BUSY        = "sys_disk_busy_percent";
    constexpr const char* NET_RECV         = "sys_network_recv_bytes_per_sec";
    constexpr const char* NET_SENT         = "sys_network_sent_bytes_per_sec";
    constexpr const char* SELF_CPU         = "telemetry_self_cpu_percent";
    constexpr const char* SELF_RSS         = "telemetry_self_memory_rss_bytes";
    constexpr const char* SELF_POLL_MS     = "telemetry_poll_duration_ms";
    constexpr const char* PROC_CPU         = "proc_cpu_percent";
    constexpr const char* PROC_RSS         = "proc_memory_rss_bytes";
    constexpr const char* PROC_GPU_SM      = "proc_gpu_sm_util_percent";
    constexpr const char* PROC_GPU_PDH     = "proc_gpu_pdh_util_percent";
    constexpr const char* PROC_THREADS     = "proc_threads";
    constexpr const char* PROC_HANDLES     = "proc_handles";
}
