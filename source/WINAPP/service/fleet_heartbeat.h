#pragma once
#include <atomic>

namespace Service {

void FleetHeartbeatRun(std::atomic<bool>& stop);

} // namespace Service
