#pragma once
#include <cstdint>
#include <cstring>
#include <algorithm>

// Fixed-depth circular ring buffer for one telemetry metric.
// Single-writer (service poll loop) / multi-reader (GUI, HTTP, pipe clients).
// No mutex — seqlock in ShmHeader protects multi-field consistency at the block level.
// DEPTH = 300: at 5s poll interval this is 25 minutes of rolling history.

struct alignas(8) MetricRing {
    static constexpr uint32_t DEPTH = 300;

    uint64_t head;          // index of most-recently written slot
    uint64_t count;         // slots written so far, saturates at DEPTH
    double   current;       // cached latest value (read without traversal)
    double   min_session;   // session-wide min
    double   max_session;   // session-wide max
    double   values[DEPTH]; // circular buffer, oldest = (head+1) % DEPTH when full

    void reset() noexcept {
        head = 0; count = 0;
        current = min_session = max_session = 0.0;
        std::memset(values, 0, sizeof(values));
    }

    void push(double v) noexcept {
        head = (head + 1u) % DEPTH;
        values[head] = v;
        current = v;
        if (count == 0) {
            min_session = max_session = v;
        } else {
            if (v < min_session) min_session = v;
            if (v > max_session) max_session = v;
        }
        if (count < DEPTH) ++count;
    }

    // Read the last N values into out[], oldest-first. Returns count written.
    uint32_t read_last(double* out, uint32_t n) const noexcept {
        uint64_t avail = (count < n) ? count : static_cast<uint64_t>(n);
        for (uint64_t i = 0; i < avail; ++i) {
            uint64_t idx = (head + DEPTH - avail + 1u + i) % DEPTH;
            out[i] = values[idx];
        }
        return static_cast<uint32_t>(avail);
    }

    // Average of last N values (or all available).
    double avg_last(uint32_t n) const noexcept {
        if (count == 0) return 0.0;
        uint64_t avail = (count < n) ? count : static_cast<uint64_t>(n);
        double sum = 0.0;
        for (uint64_t i = 0; i < avail; ++i) {
            uint64_t idx = (head + DEPTH - avail + 1u + i) % DEPTH;
            sum += values[idx];
        }
        return sum / static_cast<double>(avail);
    }
};

static_assert(sizeof(MetricRing) == 5 * 8 + MetricRing::DEPTH * 8,
    "MetricRing layout unexpected — check alignment");
