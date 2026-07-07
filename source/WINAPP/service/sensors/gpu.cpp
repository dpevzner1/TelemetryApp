#include "gpu.h"
#include "gpu_nvml.h"
#include "gpu_adl.h"
#include "gpu_igcl.h"
#include <algorithm>

// GPU subsystem dispatcher — probes all vendor DLLs at init.
// PDH GPU Engine util is injected by the poll loop after the PDH query completes.

namespace Sensors {

static int s_gpu_count = 0;

bool GpuInit() {
    bool any = false;
    if (NvmlInit()) any = true;
    if (AdlInit())  any = true;
    if (IgclInit()) any = true;
    return any; // returns false only if NO GPU vendor SDK is available (PDH still works)
}

void GpuPoll(std::vector<GpuSnapshot>& snaps) {
    snaps.clear();
    NvmlPoll(snaps);
    AdlPoll(snaps);
    IgclPoll(snaps);
    s_gpu_count = static_cast<int>(snaps.size());
}

void GpuPollPerProcess(const std::vector<uint32_t>& pids, std::vector<ProcGpuData>& out) {
    out.clear();
    NvmlPollPerProcess(pids, out); // NVML is the only per-process source currently
    // ADL and IGCL per-process metrics would go here when/if added
}

void GpuShutdown() {
    NvmlShutdown();
    AdlShutdown();
    IgclShutdown();
}

int GpuCount() { return s_gpu_count; }

} // namespace Sensors
