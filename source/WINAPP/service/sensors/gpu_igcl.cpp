#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>
#include <algorithm>
#include "gpu_igcl.h"
#include "igcl_minimal.h"

namespace Sensors {

static HMODULE           s_dll   = nullptr;
static IgclFuncs         s_fn{};
static ctl_api_handle_t  s_api   = nullptr;
static std::vector<ctl_device_adapter_handle_t> s_devs;
static bool              s_ready = false;

static void Resolve(const char* sym, void** dst) {
    *dst = reinterpret_cast<void*>(GetProcAddress(s_dll, sym));
}
#define R(fn,sym) Resolve(sym, reinterpret_cast<void**>(&s_fn.fn))

bool IgclInit() {
    if (s_ready) return true;
    s_dll = LoadLibraryExW(L"ControlLib.dll", nullptr, LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!s_dll) return false;

    R(Init,              "ctlInit");
    R(Close,             "ctlClose");
    R(EnumerateDevices,  "ctlEnumerateDevices");
    R(GetDeviceProperties,"ctlGetDeviceProperties");
    R(GetTelemetry,      "ctlGetTelemetry");

    if (!s_fn.Init || !s_fn.EnumerateDevices) {
        FreeLibrary(s_dll); s_dll = nullptr; return false;
    }

    ctl_init_args_t args{};
    args.Size       = sizeof(args);
    args.AppVersion = 1;
    if (s_fn.Init(&args, &s_api) != CTL_RESULT_SUCCESS) {
        FreeLibrary(s_dll); s_dll = nullptr; return false;
    }

    uint32_t count = 0;
    s_fn.EnumerateDevices(s_api, &count, nullptr);
    if (count == 0) {
        s_fn.Close(s_api);
        FreeLibrary(s_dll); s_dll = nullptr; return false;
    }
    s_devs.resize(count);
    s_fn.EnumerateDevices(s_api, &count, s_devs.data());

    s_ready = true;
    return true;
}

void IgclShutdown() {
    if (!s_ready) return;
    if (s_fn.Close) s_fn.Close(s_api);
    FreeLibrary(s_dll);
    s_dll = nullptr;
    s_ready = false;
}

bool IgclAvailable() { return s_ready; }

void IgclPoll(std::vector<GpuSnapshot>& snaps) {
    if (!s_ready) return;
    int gpu_idx = static_cast<int>(snaps.size());

    for (auto dev : s_devs) {
        ctl_device_adapter_properties_t props{};
        props.Size = sizeof(props);
        if (s_fn.GetDeviceProperties && s_fn.GetDeviceProperties(dev, &props) != CTL_RESULT_SUCCESS) continue;
        if (props.pci_vendor_id != 0x8086) continue; // Intel vendor ID

        GpuSnapshot g{};
        g.index      = gpu_idx++;
        g.vendor     = GpuVendor::INTEL;
        g.vendor_str = "Intel";
        g.name       = props.name;

        ctl_telemetry_data_t telem{};
        if (s_fn.GetTelemetry && s_fn.GetTelemetry(dev, &telem) == CTL_RESULT_SUCCESS) {
            g.usage_pct       = telem.GlobalActivityCounter;
            g.vram_used_mb    = telem.GpuCurrentVram / (1024.0 * 1024.0);
            g.vram_total_mb   = telem.GpuMaxVram     / (1024.0 * 1024.0);
            g.vram_percent    = (g.vram_total_mb > 0)
                ? g.vram_used_mb / g.vram_total_mb * 100.0 : 0.0;
            g.temp_celsius    = telem.GpuCurrentTemperature;
            g.power_watts     = telem.GpuPower;
            g.clock_core_mhz  = telem.GpuCurrentClockFrequency;
            if (g.temp_celsius > 0 && g.usage_pct > 0)
                g.thermal_efficiency = g.usage_pct / g.temp_celsius;
        }
        snaps.push_back(std::move(g));
    }
}

} // namespace Sensors
