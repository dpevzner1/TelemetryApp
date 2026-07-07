#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstring>
#include "shm_writer.h"
#include "../../shared/shm_layout.h"
#include "../../shared/metric_ids.h"

namespace Service {

static HANDLE    s_map  = nullptr;
static ShmBlock* s_blk  = nullptr;

bool ShmOpen() {
    SECURITY_DESCRIPTOR sd{};
    SECURITY_ATTRIBUTES sa{};
    InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(&sd, TRUE, nullptr, FALSE);
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = &sd;
    sa.bInheritHandle = FALSE;

    s_map = CreateFileMappingW(
        INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE,
        0, static_cast<DWORD>(SHM_SIZE), SHM_NAME);
    if (!s_map) return false;

    s_blk = reinterpret_cast<ShmBlock*>(
        MapViewOfFile(s_map, FILE_MAP_ALL_ACCESS, 0, 0, SHM_SIZE));
    if (!s_blk) { CloseHandle(s_map); s_map = nullptr; return false; }

    // First-time init: zero the block and set magic
    bool created = (GetLastError() != ERROR_ALREADY_EXISTS);
    if (created) {
        std::memset(s_blk, 0, SHM_SIZE);
        s_blk->hdr.magic   = 0x54454C4D; // 'TELM'
        s_blk->hdr.version = 1;
        s_blk->hdr.write_seq.store(0, std::memory_order_release);
        // Init each MetricRing
        for (int i = 0; i < MetricId::METRIC_COUNT; ++i)
            s_blk->metrics[i].reset();
    }
    return true;
}

void ShmClose() {
    if (s_blk) { UnmapViewOfFile(s_blk); s_blk = nullptr; }
    if (s_map) { CloseHandle(s_map);      s_map = nullptr; }
}

ShmBlock* ShmGet() { return s_blk; }

void ShmBeginWrite(ShmBlock* b) {
    // Increment to odd — signals readers to spin
    b->hdr.write_seq.fetch_add(1, std::memory_order_release);
    // Full memory fence before any metric write
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

void ShmEndWrite(ShmBlock* b) {
    // Full memory fence after last metric write
    std::atomic_thread_fence(std::memory_order_seq_cst);
    // Increment to even — readers can proceed
    b->hdr.write_seq.fetch_add(1, std::memory_order_release);
}

void ShmPush(ShmBlock* b, uint32_t metric_id, double value) {
    if (metric_id >= MetricId::METRIC_COUNT) return;
    b->metrics[metric_id].push(value);
}

} // namespace Service
