#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdlib>
#include <algorithm>
#include "gpu_adl.h"
#include "adl_minimal.h"

namespace Sensors {

static HMODULE       s_dll   = nullptr;
static AdlFuncs      s_fn{};
static ADL_CONTEXT_HANDLE s_ctx = nullptr;
static bool          s_ready = false;

static void Resolve(const char* sym, void** dst) {
    *dst = reinterpret_cast<void*>(GetProcAddress(s_dll, sym));
}
#define R(fn,sym) Resolve(sym, reinterpret_cast<void**>(&s_fn.fn))

bool AdlInit() {
    if (s_ready) return true;
    s_dll = LoadLibraryExW(L"atiadlxx.dll", nullptr, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!s_dll) return false;

    R(Create,          "ADL2_Main_Control_Create");
    R(Destroy,         "ADL2_Main_Control_Destroy");
    R(GetAdapterCount, "ADL2_Adapter_NumberOfAdapters_Get");
    R(GetAdapterInfo,  "ADL2_Adapter_AdapterInfo_Get");
    R(GetAdapterActive,"ADL2_Adapter_Active_Get");
    R(GetTemp,         "ADL2_Overdrive5_Temperature_Get");
    R(GetFan,          "ADL2_Overdrive5_FanSpeed_Get");
    R(GetActivity,     "ADL2_Overdrive5_CurrentActivity_Get");
    R(GetMemInfo,      "ADL2_Adapter_MemoryInfo_Get");

    if (!s_fn.Create || !s_fn.GetAdapterCount) {
        FreeLibrary(s_dll); s_dll = nullptr; return false;
    }
    if (s_fn.Create(adl_malloc, 1, &s_ctx) != ADL_OK) {
        FreeLibrary(s_dll); s_dll = nullptr; return false;
    }
    s_ready = true;
    return true;
}

void AdlShutdown() {
    if (!s_ready) return;
    if (s_fn.Destroy) s_fn.Destroy(s_ctx);
    FreeLibrary(s_dll);
    s_dll = nullptr;
    s_ready = false;
}

bool AdlAvailable() { return s_ready; }

void AdlPoll(std::vector<GpuSnapshot>& snaps) {
    if (!s_ready) return;
    int count = 0;
    s_fn.GetAdapterCount(s_ctx, &count);
    if (count <= 0) return;

    std::vector<AdapterInfo> infos(count);
    s_fn.GetAdapterInfo(s_ctx, infos.data(), sizeof(AdapterInfo) * count);

    int gpu_idx = static_cast<int>(snaps.size()); // continue GPU index after NVML
    for (int i = 0; i < count; ++i) {
        int active = 0;
        if (s_fn.GetAdapterActive) s_fn.GetAdapterActive(s_ctx, i, &active);
        if (!active || infos[i].iVendorID != 0x1002) continue; // 0x1002 = AMD

        GpuSnapshot g{};
        g.index      = gpu_idx++;
        g.vendor     = GpuVendor::AMD;
        g.vendor_str = "AMD";
        g.name       = infos[i].strAdapterName;

        ADLPMActivity act{};
        act.iSize = sizeof(act);
        if (s_fn.GetActivity && s_fn.GetActivity(s_ctx, i, &act) == ADL_OK) {
            g.usage_pct       = act.iActivityPercent;
            g.clock_core_mhz  = act.iEngineClock / 100.0;  // 10kHz units → MHz
            g.clock_mem_mhz   = act.iMemoryClock / 100.0;
        }

        ADLTemperature temp{};
        temp.iSize = sizeof(temp);
        if (s_fn.GetTemp && s_fn.GetTemp(s_ctx, i, 0, &temp) == ADL_OK)
            g.temp_celsius = temp.iTemperature / 1000.0;

        ADLFanSpeedValue fan{};
        fan.iSize      = sizeof(fan);
        fan.iSpeedType = ADL_DL_FANCTRL_SPEED_TYPE_PERCENT;
        if (s_fn.GetFan && s_fn.GetFan(s_ctx, i, 0, &fan) == ADL_OK)
            g.fan_pct = fan.iFanSpeed;

        ADLMemoryInfo mem{};
        if (s_fn.GetMemInfo && s_fn.GetMemInfo(s_ctx, i, &mem) == ADL_OK)
            g.vram_total_mb = static_cast<double>(mem.iMemorySize) / (1024.0 * 1024.0);

        if (g.temp_celsius > 0 && g.usage_pct > 0)
            g.thermal_efficiency = g.usage_pct / g.temp_celsius;

        snaps.push_back(std::move(g));
    }
}

} // namespace Sensors
