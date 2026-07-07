#pragma once
#include <vector>
#include "gpu.h"

namespace Sensors {
bool IgclInit();
void IgclShutdown();
bool IgclAvailable();
void IgclPoll(std::vector<GpuSnapshot>& snaps);
} // namespace Sensors
