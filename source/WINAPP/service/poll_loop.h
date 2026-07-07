#pragma once
#include <atomic>
#include <cstdint>

namespace Service {

// Single poll-loop iteration result
struct PollMetrics {
    double poll_duration_ms;
    int    active_gpus;
    int    active_disks;
    int    active_nics;
    int    active_temps;
    int    active_watches;
};

// Called once from ServiceMain after all sensors init
bool PollLoopInit();

// Main poll loop — runs until *stop becomes true.
// Blocks the calling thread. Designed to run on a dedicated thread.
void PollLoopRun(std::atomic<bool>& stop);

void PollLoopShutdown();

} // namespace Service
