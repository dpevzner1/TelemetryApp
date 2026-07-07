#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <ctime>
#include "gpu_nvml.h"

namespace Sensors {

static HMODULE             s_dll     = nullptr;
static NvmlFuncs           s_fn{};
static std::vector<nvmlDevice_t> s_handles;
static std::vector<int>    s_prev_mem_clk; // per-device previous mem clock MHz
static std::vector<int>    s_mem_clk_transitions;
static bool                s_ready   = false;

// CUDA architecture name + tensor core generation tables
struct CudaArch { int maj, min; const char* name; int tensor_gen; };
static constexpr CudaArch kArchTable[] = {
    {9, 0, "Hopper",       4},
    {8, 9, "Ada Lovelace", 4},   // RTX 4070 Laptop = this
    {8, 6, "Ampere",       3},
    {8, 0, "Ampere",       3},
    {7, 5, "Turing",       2},
    {7, 0, "Volta",        1},
    {6, 1, "Pascal",       0},
    {6, 0, "Pascal",       0},
};

static void ResolveFunc(HMODULE dll, void** dst, const char* name) {
    *dst = reinterpret_cast<void*>(GetProcAddress(dll, name));
}

#define RES(fn, sym) ResolveFunc(s_dll, reinterpret_cast<void**>(&s_fn.fn), sym)

bool NvmlInit() {
    if (s_ready) return true;

    // Try standard NVIDIA driver location first, then PATH
    s_dll = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!s_dll) {
        wchar_t path[MAX_PATH];
        ExpandEnvironmentStringsW(
            L"%ProgramW6432%\\NVIDIA Corporation\\NVSMI\\nvml.dll", path, MAX_PATH);
        s_dll = LoadLibraryW(path);
    }
    if (!s_dll) return false;

    RES(Init,           "nvmlInit_v2");
    if (!s_fn.Init) RES(Init, "nvmlInit");
    RES(Shutdown,       "nvmlShutdown");
    RES(GetCount,       "nvmlDeviceGetCount_v2");
    RES(GetHandle,      "nvmlDeviceGetHandleByIndex_v2");
    RES(GetName,        "nvmlDeviceGetName");
    RES(GetMemInfo,     "nvmlDeviceGetMemoryInfo");
    RES(GetUtil,        "nvmlDeviceGetUtilizationRates");
    RES(GetTemp,        "nvmlDeviceGetTemperature");
    RES(GetPower,       "nvmlDeviceGetPowerUsage");
    RES(GetClock,       "nvmlDeviceGetClockInfo");
    RES(GetFan,         "nvmlDeviceGetFanSpeed");
    RES(GetThrottle,    "nvmlDeviceGetCurrentClocksThrottleReasons");
    RES(GetComputeProcs,"nvmlDeviceGetComputeRunningProcesses_v3");
    if (!s_fn.GetComputeProcs) RES(GetComputeProcs,"nvmlDeviceGetComputeRunningProcesses");
    RES(GetGraphicsProcs,"nvmlDeviceGetGraphicsRunningProcesses_v3");
    if (!s_fn.GetGraphicsProcs) RES(GetGraphicsProcs,"nvmlDeviceGetGraphicsRunningProcesses");
    RES(GetProcUtil,    "nvmlDeviceGetProcessUtilization");
    RES(GetEncoder,     "nvmlDeviceGetEncoderUtilization");
    RES(GetDecoder,     "nvmlDeviceGetDecoderUtilization");
    RES(GetCudaCC,      "nvmlDeviceGetCudaComputeCapability");

    if (!s_fn.Init || !s_fn.GetCount || !s_fn.GetHandle) {
        FreeLibrary(s_dll); s_dll = nullptr; return false;
    }

    if (s_fn.Init() != NVML_SUCCESS) {
        FreeLibrary(s_dll); s_dll = nullptr; return false;
    }

    unsigned int count = 0;
    s_fn.GetCount(&count);
    s_handles.resize(count);
    for (unsigned int i = 0; i < count; ++i) s_fn.GetHandle(i, &s_handles[i]);
    s_prev_mem_clk.assign(count, 0);
    s_mem_clk_transitions.assign(count, 0);

    s_ready = true;
    return true;
}

void NvmlShutdown() {
    if (!s_ready) return;
    if (s_fn.Shutdown) s_fn.Shutdown();
    FreeLibrary(s_dll);
    s_dll = nullptr;
    s_handles.clear();
    s_ready = false;
}

bool NvmlAvailable()  { return s_ready; }
int  NvmlDeviceCount() { return static_cast<int>(s_handles.size()); }
const std::vector<nvmlDevice_t>& NvmlGetHandles() { return s_handles; }
const NvmlFuncs& NvmlGetFuncs() { return s_fn; }

void NvmlPoll(std::vector<GpuSnapshot>& snaps) {
    if (!s_ready) return;

    for (int i = 0; i < static_cast<int>(s_handles.size()); ++i) {
        nvmlDevice_t h = s_handles[i];
        GpuSnapshot g{};
        g.index  = i;
        g.vendor = GpuVendor::NVIDIA;
        g.vendor_str = "NVIDIA";

        char name[96]{};
        if (s_fn.GetName) s_fn.GetName(h, name, sizeof(name));
        g.name = name;

        // Memory
        nvmlMemory_t mem{};
        if (s_fn.GetMemInfo && s_fn.GetMemInfo(h, &mem) == NVML_SUCCESS) {
            g.vram_used_mb  = static_cast<double>(mem.used)  / (1024.0 * 1024.0);
            g.vram_total_mb = static_cast<double>(mem.total) / (1024.0 * 1024.0);
            g.vram_percent  = mem.total ? mem.used * 100.0 / mem.total : 0.0;
        }

        // Utilization
        nvmlUtilization_t util{};
        if (s_fn.GetUtil && s_fn.GetUtil(h, &util) == NVML_SUCCESS) {
            g.usage_pct       = util.gpu;
            g.mem_bw_util_pct = util.memory;
        }
        g.sm_util_pct = g.usage_pct; // nvmlGetUtilizationRates.gpu is SM util

        // Temperature
        unsigned int temp = 0;
        if (s_fn.GetTemp && s_fn.GetTemp(h, NVML_TEMPERATURE_GPU, &temp) == NVML_SUCCESS)
            g.temp_celsius = temp;

        // Power
        unsigned int pw_mw = 0;
        if (s_fn.GetPower && s_fn.GetPower(h, &pw_mw) == NVML_SUCCESS)
            g.power_watts = pw_mw / 1000.0;

        // Fan
        unsigned int fan = 0;
        if (s_fn.GetFan && s_fn.GetFan(h, &fan) == NVML_SUCCESS)
            g.fan_pct = fan;

        // Clocks
        unsigned int core_clk = 0, mem_clk = 0;
        if (s_fn.GetClock) {
            s_fn.GetClock(h, NVML_CLOCK_GRAPHICS, &core_clk);
            s_fn.GetClock(h, NVML_CLOCK_MEM,      &mem_clk);
        }
        g.clock_core_mhz = core_clk;
        g.clock_mem_mhz  = mem_clk;

        // Memory clock P-state transitions
        int prev = s_prev_mem_clk[i];
        if (prev > 0 && static_cast<int>(mem_clk) != prev)
            s_mem_clk_transitions[i]++;
        s_prev_mem_clk[i]     = static_cast<int>(mem_clk);
        g.mem_clk_transitions = s_mem_clk_transitions[i];

        // Encoder / decoder
        unsigned int enc = 0, dec = 0, period = 0;
        if (s_fn.GetEncoder) s_fn.GetEncoder(h, &enc, &period);
        if (s_fn.GetDecoder) s_fn.GetDecoder(h, &dec, &period);
        g.encoder_pct = enc;
        g.decoder_pct = dec;

        // Throttle bitmask
        unsigned long long reasons = 0;
        if (s_fn.GetThrottle && s_fn.GetThrottle(h, &reasons) == NVML_SUCCESS) {
            constexpr unsigned long long THERMAL =
                NVML_CLOCKS_THROTTLE_REASON_HW_SLOWDOWN |
                NVML_CLOCKS_THROTTLE_REASON_SW_THERMAL_SLOWDOWN |
                NVML_CLOCKS_THROTTLE_REASON_HW_THERMAL_SLOWDOWN;
            constexpr unsigned long long POWER =
                NVML_CLOCKS_THROTTLE_REASON_SW_POWER_CAP |
                NVML_CLOCKS_THROTTLE_REASON_HW_POWER_BRAKE_SLOWDOWN;
            g.throttle_active = !!(reasons & (THERMAL | POWER));
            if (reasons & NVML_CLOCKS_THROTTLE_REASON_HW_SLOWDOWN)         g.throttle_reasons.push_back("hw_slowdown");
            if (reasons & NVML_CLOCKS_THROTTLE_REASON_SW_THERMAL_SLOWDOWN) g.throttle_reasons.push_back("sw_thermal");
            if (reasons & NVML_CLOCKS_THROTTLE_REASON_HW_THERMAL_SLOWDOWN) g.throttle_reasons.push_back("hw_thermal");
            if (reasons & NVML_CLOCKS_THROTTLE_REASON_SW_POWER_CAP)        g.throttle_reasons.push_back("sw_power_cap");
            if (reasons & NVML_CLOCKS_THROTTLE_REASON_HW_POWER_BRAKE_SLOWDOWN) g.throttle_reasons.push_back("hw_power_brake");
        }

        // Thermal efficiency: util per °C
        if (g.temp_celsius > 0)
            g.thermal_efficiency = g.usage_pct / g.temp_celsius;

        // CUDA compute capability + tensor core metadata
        int cc_maj = 0, cc_min = 0;
        if (s_fn.GetCudaCC && s_fn.GetCudaCC(h, &cc_maj, &cc_min) == NVML_SUCCESS) {
            g.cuda_cc_major = cc_maj;
            g.cuda_cc_minor = cc_min;
            g.has_tensor_cores = (cc_maj >= 7);
            for (const auto& a : kArchTable) {
                if (a.maj == cc_maj && a.min == cc_min) {
                    g.tensor_core_gen = a.tensor_gen;
                    break;
                }
                if (a.maj == cc_maj) {
                    g.tensor_core_gen = a.tensor_gen;
                    break;
                }
            }
        }

        snaps.push_back(std::move(g));
    }
}

void NvmlPollPerProcess(const std::vector<uint32_t>& pids, std::vector<ProcGpuData>& out) {
    if (!s_ready || pids.empty()) return;

    // Per-process VRAM: use max() across Compute + Graphics contexts (avoids double-counting)
    auto find_or_add = [&](uint32_t pid) -> ProcGpuData& {
        for (auto& e : out) if (e.pid == pid) return e;
        out.push_back({pid, 0, 0, 0, 0, 0});
        return out.back();
    };

    unsigned long long since_us =
        static_cast<unsigned long long>((time(nullptr) - 6)) * 1'000'000ULL;

    for (auto h : s_handles) {
        // VRAM per process
        if (s_fn.GetComputeProcs) {
            unsigned int count = 64;
            nvmlProcessInfo_t infos[64]{};
            if (s_fn.GetComputeProcs(h, &count, infos) == NVML_SUCCESS) {
                for (unsigned int k = 0; k < count; ++k) {
                    if (std::find(pids.begin(), pids.end(), infos[k].pid) == pids.end()) continue;
                    auto& e = find_or_add(infos[k].pid);
                    e.vram_bytes = std::max(e.vram_bytes, infos[k].usedGpuMemory);
                }
            }
        }
        if (s_fn.GetGraphicsProcs) {
            unsigned int count = 64;
            nvmlProcessInfo_t infos[64]{};
            if (s_fn.GetGraphicsProcs(h, &count, infos) == NVML_SUCCESS) {
                for (unsigned int k = 0; k < count; ++k) {
                    if (std::find(pids.begin(), pids.end(), infos[k].pid) == pids.end()) continue;
                    auto& e = find_or_add(infos[k].pid);
                    e.vram_bytes = std::max(e.vram_bytes, infos[k].usedGpuMemory);
                }
            }
        }

        // Per-process SM util via nvmlDeviceGetProcessUtilization
        if (s_fn.GetProcUtil) {
            unsigned int count = 64;
            nvmlProcessUtilizationSample_t samples[64]{};
            if (s_fn.GetProcUtil(h, samples, &count, since_us) == NVML_SUCCESS) {
                for (unsigned int k = 0; k < count; ++k) {
                    if (std::find(pids.begin(), pids.end(), samples[k].pid) == pids.end()) continue;
                    auto& e = find_or_add(samples[k].pid);
                    e.sm_util  = std::max(e.sm_util,  samples[k].smUtil);
                    e.mem_util = std::max(e.mem_util, samples[k].memUtil);
                    e.enc_util = std::max(e.enc_util, samples[k].encUtil);
                    e.dec_util = std::max(e.dec_util, samples[k].decUtil);
                }
            }
        }
    }
}

} // namespace Sensors
