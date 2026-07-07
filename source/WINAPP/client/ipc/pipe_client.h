#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <atomic>

namespace Client {

// Delta frame from the service — current-value snapshot pushed ~1Hz
struct PipeMetric {
    uint32_t id;
    double   value;
};

struct PipeFrame {
    uint64_t ts_ms;
    std::vector<PipeMetric> metrics;
};

using FrameCallback = std::function<void(const PipeFrame&)>;

// Connects to named pipe and calls cb on each received frame.
// Runs on the caller's thread — designed to run on a dedicated background thread.
void PipeClientRun(std::atomic<bool>& stop, FrameCallback cb);

} // namespace Client
