#pragma once
#include <atomic>

namespace Service {

bool HttpServerInit();
void HttpServerRun(std::atomic<bool>& stop);
void HttpServerShutdown();

} // namespace Service
