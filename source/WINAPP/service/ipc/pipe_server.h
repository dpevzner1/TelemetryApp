#pragma once
#include <atomic>

namespace Service {

bool PipeServerInit();
void PipeServerRun(std::atomic<bool>& stop);
void PipeServerShutdown();

} // namespace Service
