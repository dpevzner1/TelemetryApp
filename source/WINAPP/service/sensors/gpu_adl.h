#pragma once
#include <vector>
#include "gpu.h"

namespace Sensors {
bool AdlInit();
void AdlShutdown();
bool AdlAvailable();
void AdlPoll(std::vector<GpuSnapshot>& snaps);
} // namespace Sensors
