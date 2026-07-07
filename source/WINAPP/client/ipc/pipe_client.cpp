#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <vector>
#include <cstring>
#include "pipe_client.h"
#include "../../shared/shm_layout.h"

namespace Client {

// Must match pipe_server.cpp frame layout
struct RawPipeHdr {
    uint32_t magic;
    uint32_t version;
    uint64_t ts_ms;
    uint32_t n_metrics;
};
struct RawMetric {
    uint32_t id;
    double   value;
};

static bool ReadAll(HANDLE pipe, void* buf, DWORD len) {
    DWORD total = 0;
    while (total < len) {
        DWORD got = 0;
        if (!ReadFile(pipe, static_cast<char*>(buf) + total, len - total, &got, nullptr) || got == 0)
            return false;
        total += got;
    }
    return true;
}

void PipeClientRun(std::atomic<bool>& stop, FrameCallback cb) {
    while (!stop.load(std::memory_order_acquire)) {
        HANDLE pipe = CreateFileW(
            PIPE_NAME,
            GENERIC_READ,
            0, nullptr,
            OPEN_EXISTING,
            0, nullptr);

        if (pipe == INVALID_HANDLE_VALUE) {
            // Service not yet started — wait and retry
            WaitNamedPipeW(PIPE_NAME, 1000);
            continue;
        }

        while (!stop.load(std::memory_order_acquire)) {
            RawPipeHdr hdr{};
            if (!ReadAll(pipe, &hdr, sizeof(hdr))) break;
            if (hdr.magic != 0x54454C4D || hdr.version != 1) break;

            std::vector<RawMetric> raw(hdr.n_metrics);
            if (hdr.n_metrics > 0 && !ReadAll(pipe, raw.data(),
                                               hdr.n_metrics * sizeof(RawMetric))) break;

            PipeFrame frame;
            frame.ts_ms = hdr.ts_ms;
            frame.metrics.reserve(hdr.n_metrics);
            for (auto& m : raw) frame.metrics.push_back({m.id, m.value});
            cb(frame);
        }

        CloseHandle(pipe);
        Sleep(500); // brief pause before reconnect
    }
}

} // namespace Client
