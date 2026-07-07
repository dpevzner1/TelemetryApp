#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstring>
#include "shm_reader.h"
#include "../../shared/shm_layout.h"
#include "../../shared/metric_ids.h"

namespace Client {

static HANDLE          s_map = nullptr;
static const ShmBlock* s_blk = nullptr;

bool ShmReaderOpen() {
    s_map = OpenFileMappingW(FILE_MAP_READ, FALSE, SHM_NAME);
    if (!s_map) return false;
    s_blk = reinterpret_cast<const ShmBlock*>(
        MapViewOfFile(s_map, FILE_MAP_READ, 0, 0, SHM_SIZE));
    if (!s_blk) { CloseHandle(s_map); s_map = nullptr; return false; }
    return true;
}

void ShmReaderClose() {
    if (s_blk) { UnmapViewOfFile(const_cast<ShmBlock*>(s_blk)); s_blk = nullptr; }
    if (s_map) { CloseHandle(s_map); s_map = nullptr; }
}

const ShmBlock* ShmReaderGet() { return s_blk; }

bool ShmReadMetric(uint32_t metric_id, double& out) {
    if (!s_blk || metric_id >= MetricId::METRIC_COUNT) return false;
    for (int retries = 8; retries > 0; --retries) {
        uint64_t seq0 = s_blk->hdr.write_seq.load(std::memory_order_acquire);
        if (seq0 & 1) { Sleep(1); continue; }
        out = s_blk->metrics[metric_id].current;
        uint64_t seq1 = s_blk->hdr.write_seq.load(std::memory_order_acquire);
        if (seq1 == seq0) return true;
    }
    return false;
}

uint32_t ShmReadHistory(uint32_t metric_id, double* out, uint32_t n) {
    if (!s_blk || metric_id >= MetricId::METRIC_COUNT) return 0;
    for (int retries = 8; retries > 0; --retries) {
        uint64_t seq0 = s_blk->hdr.write_seq.load(std::memory_order_acquire);
        if (seq0 & 1) { Sleep(1); continue; }
        uint32_t got = s_blk->metrics[metric_id].read_last(out, n);
        uint64_t seq1 = s_blk->hdr.write_seq.load(std::memory_order_acquire);
        if (seq1 == seq0) return got;
    }
    return 0;
}

} // namespace Client
