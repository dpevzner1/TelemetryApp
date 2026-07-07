#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <cstdint>
#include "ring_buffer.h"
#include "metric_ids.h"

// ── Named object constants ────────────────────────────────────────────────────
#define SHM_NAME         L"Global\\TelemetryApp_SHM_v1"
#define SHM_MUTEX_NAME   L"Global\\TelemetryApp_SHM_Mutex"
#define PIPE_NAME        L"\\\\.\\pipe\\TelemetryApp"
#define HTTP_PORT        8765
#define SHM_MAGIC        0x54454C4Du   // "TELM"
#define SHM_VERSION      1u

// ── String table limits ───────────────────────────────────────────────────────
#define SHM_GPU_NAME_LEN    64
#define SHM_DISK_NAME_LEN   32
#define SHM_NIC_NAME_LEN    48
#define SHM_WATCH_LABEL_LEN 64
#define SHM_TEMP_NAME_LEN   64
#define SHM_VER_LEN         32

// ── Shared memory header ──────────────────────────────────────────────────────
// Padded to 64B cache-line boundary so metrics array starts on its own line.
// write_seq implements a seqlock: odd during write, even (and stable) when readable.
// Reader pattern:
//   uint64_t seq;
//   do { while (seq & 1) seq = shm->hdr.write_seq.load(acquire);
//        <read fields>
//   } while (shm->hdr.write_seq.load(acquire) != seq);
struct alignas(64) ShmHeader {
    uint32_t magic;                                         //  0
    uint32_t version;                                       //  4
    std::atomic<uint64_t> write_seq;                        //  8  (seqlock counter)
    uint64_t  ts_poll_ms;                                   // 16  Unix ms of last poll
    uint32_t  poll_duration_ms;                             // 24
    uint32_t  active_gpu_count;                             // 28
    uint32_t  active_disk_count;                            // 32
    uint32_t  active_nic_count;                             // 36
    uint32_t  active_watch_count;                           // 40
    uint32_t  active_temp_count;                            // 44
    uint32_t  active_cpu_cores;                             // 48
    uint8_t   service_alive;                                // 52
    uint8_t   _pad[3];                                      // 53
    uint32_t  _reserved;                                    // 56
    // 60–63 pad to 64B

    // Label tables (read-only after service init, no seqlock needed for these)
    char gpu_name   [MetricId::GPU_MAX_COUNT ][SHM_GPU_NAME_LEN ]; // 4×64  = 256B
    char disk_name  [MetricId::DISK_MAX_COUNT][SHM_DISK_NAME_LEN]; // 8×32  = 256B
    char nic_name   [MetricId::NET_MAX_COUNT ][SHM_NIC_NAME_LEN ]; // 8×48  = 384B
    char watch_label[MetricId::WATCH_MAX_COUNT][SHM_WATCH_LABEL_LEN]; // 8×64 = 512B
    char temp_name  [MetricId::TEMP_MAX_COUNT][SHM_TEMP_NAME_LEN]; // 32×64 = 2048B
    char service_ver[SHM_VER_LEN];                                  // 32B
};

// ── Complete shared memory block ──────────────────────────────────────────────
struct alignas(64) ShmBlock {
    ShmHeader  hdr;
    MetricRing metrics[MetricId::METRIC_COUNT];
};

// Total SHM size:
//   ShmHeader  ≈ 64 + 256 + 256 + 384 + 512 + 2048 + 32 = ~3552B → rounds to 4KB
//   MetricRing = (4+4+8+8+8 + 300×8) = 32 + 2400 = 2432B each
//   512 rings  = 512 × 2432 = 1,245,184B ≈ 1.19MB
//   Total      ≈ 1.19MB  (well within mapping budget)
constexpr SIZE_T SHM_SIZE = sizeof(ShmBlock);

// ── Seqlock writer helpers (service side) ─────────────────────────────────────
inline void shm_write_begin(ShmBlock* shm) noexcept {
    shm->hdr.write_seq.fetch_add(1u, std::memory_order_release); // → odd
}
inline void shm_write_end(ShmBlock* shm) noexcept {
    shm->hdr.write_seq.fetch_add(1u, std::memory_order_release); // → even
}

// ── Seqlock reader helpers (client side) ──────────────────────────────────────
// Returns true and saves seq when the block is stable (even seq).
inline bool shm_stable(const ShmBlock* shm, uint64_t& seq) noexcept {
    seq = shm->hdr.write_seq.load(std::memory_order_acquire);
    return !(seq & 1u);
}
// Returns true if seq hasn't changed since shm_stable().
inline bool shm_consistent(const ShmBlock* shm, uint64_t saved) noexcept {
    std::atomic_thread_fence(std::memory_order_acquire);
    return shm->hdr.write_seq.load(std::memory_order_relaxed) == saved;
}

// Convenience: push a value into a ring and update the header's ts.
// Call only between shm_write_begin / shm_write_end.
inline void shm_push(ShmBlock* shm, uint32_t metric_id, double v) noexcept {
    if (metric_id < MetricId::METRIC_COUNT)
        shm->metrics[metric_id].push(v);
}
