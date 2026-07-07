#pragma once
#include <vector>
#include <cstdint>
#include "gpu.h"
#include "nvml_minimal.h"

namespace Sensors {

// NVIDIA GPU sub-driver — loaded dynamically from nvml.dll.
// Owns the NVML init/shutdown lifecycle; no other module calls nvmlInit/nvmlShutdown.

bool NvmlInit();
void NvmlShutdown();
bool NvmlAvailable();
int  NvmlDeviceCount();

// Poll all NVIDIA GPUs into snaps[] (caller pre-sizes or appends)
void NvmlPoll(std::vector<GpuSnapshot>& snaps);

// Per-process VRAM + SM/mem utilization via nvmlDeviceGetProcessUtilization
// Uses max() across compute+graphics to avoid double-counting.
void NvmlPollPerProcess(
    const std::vector<uint32_t>& pids,
    std::vector<ProcGpuData>&    out);

// Expose handles for shared use (e.g., process watcher needs them too)
const std::vector<nvmlDevice_t>& NvmlGetHandles();
const NvmlFuncs&                  NvmlGetFuncs();

} // namespace Sensors
