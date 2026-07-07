#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <thread>
#include <vector>
#include <cstring>
#include <cstdint>
#include "pipe_server.h"
#include "shm_writer.h"
#include "../../shared/shm_layout.h"
#include "../../shared/metric_ids.h"

// Named pipe protocol: service pushes a fixed-size frame to every connected client
// each poll tick. Frame layout:
//   uint32_t magic    (0x54454C4D)
//   uint32_t version  (1)
//   uint64_t ts_ms    (poll timestamp)
//   uint32_t n_metrics
//   n_metrics × { uint32_t metric_id, double value }   (current values only)
//
// This delta-frame design means the pipe client gets real-time updates at ~1Hz
// without needing to map SHM. SHM is the efficient path for the GUI renderer.

namespace Service {

struct PipeFrame {
    uint32_t magic   = 0x54454C4D;
    uint32_t version = 1;
    uint64_t ts_ms   = 0;
    uint32_t n_metrics = 0;
    // followed by n_metrics × PipeMetric
};
struct PipeMetric {
    uint32_t id;
    double   value;
};

static HANDLE s_stop_event = nullptr;

static void BuildFrame(std::vector<uint8_t>& buf) {
    ShmBlock* shm = ShmGet();
    if (!shm) return;

    // Read seqlock
    uint64_t seq0;
    do {
        seq0 = shm->hdr.write_seq.load(std::memory_order_acquire);
    } while (seq0 & 1);

    uint64_t ts = shm->hdr.ts_poll_ms;
    uint32_t n  = MetricId::METRIC_COUNT;
    std::vector<PipeMetric> metrics;
    metrics.reserve(64);
    for (uint32_t i = 0; i < n; ++i) {
        double v = shm->metrics[i].current;
        if (v != 0.0) metrics.push_back({i, v});
    }

    uint64_t seq1 = shm->hdr.write_seq.load(std::memory_order_acquire);
    if (seq1 != seq0) return; // torn read; skip this frame

    PipeFrame hdr{};
    hdr.ts_ms     = ts;
    hdr.n_metrics = static_cast<uint32_t>(metrics.size());

    buf.resize(sizeof(PipeFrame) + metrics.size() * sizeof(PipeMetric));
    std::memcpy(buf.data(), &hdr, sizeof(PipeFrame));
    std::memcpy(buf.data() + sizeof(PipeFrame), metrics.data(),
                metrics.size() * sizeof(PipeMetric));
}

static void HandleClient(HANDLE pipe, HANDLE stop) {
    std::vector<uint8_t> frame;
    while (true) {
        if (WaitForSingleObject(stop, 0) == WAIT_OBJECT_0) break;
        frame.clear();
        BuildFrame(frame);
        if (!frame.empty()) {
            DWORD written = 0;
            if (!WriteFile(pipe, frame.data(), static_cast<DWORD>(frame.size()),
                           &written, nullptr)) break;
        }
        // Wait ~1 sec or until stop
        if (WaitForSingleObject(stop, 950) == WAIT_OBJECT_0) break;
    }
    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
}

bool PipeServerInit() {
    s_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    return s_stop_event != nullptr;
}

void PipeServerRun(std::atomic<bool>& stop) {
    std::vector<std::thread> clients;

    while (!stop.load(std::memory_order_acquire)) {
        HANDLE pipe = CreateNamedPipeW(
            PIPE_NAME,
            PIPE_ACCESS_OUTBOUND | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            65536, 0,
            500,  // default timeout ms for ConnectNamedPipe
            nullptr);

        if (pipe == INVALID_HANDLE_VALUE) {
            Sleep(100); continue;
        }

        // Overlapped connect so we can honor stop
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        ConnectNamedPipe(pipe, &ov);
        HANDLE waits[2] = { ov.hEvent, s_stop_event };
        DWORD w = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        CloseHandle(ov.hEvent);

        if (w != WAIT_OBJECT_0) { // stop or error
            CloseHandle(pipe); break;
        }

        // Client connected — hand off to a detached thread
        HANDLE stop_ev = s_stop_event;
        clients.emplace_back([pipe, stop_ev]{ HandleClient(pipe, stop_ev); });
    }

    SetEvent(s_stop_event);
    for (auto& t : clients) if (t.joinable()) t.join();
}

void PipeServerShutdown() {
    if (s_stop_event) { SetEvent(s_stop_event); CloseHandle(s_stop_event); s_stop_event = nullptr; }
}

} // namespace Service
