#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <psapi.h>
#include "memory.h"

#pragma comment(lib, "psapi.lib")

// SystemMemoryListInformation gives standby/modified page list breakdown
// (what Resource Monitor's "Memory" tab shows, not exposed by GlobalMemoryStatusEx)
#define SystemMemoryListInformation 80

// SystemPerformanceInformation (undocumented) — first 12 DWORDs include PageFaultCount
#define SystemPerformanceInformation 2
typedef struct {
    LARGE_INTEGER IdleProcessTime;
    LARGE_INTEGER IoReadTransferCount;
    LARGE_INTEGER IoWriteTransferCount;
    LARGE_INTEGER IoOtherTransferCount;
    ULONG IoReadOperationCount;
    ULONG IoWriteOperationCount;
    ULONG IoOtherOperationCount;
    ULONG AvailablePages;
    ULONG CommittedPages;
    ULONG CommitLimit;
    ULONG PeakCommitment;
    ULONG PageFaultCount;
    ULONG CopyOnWriteCount;
    ULONG TransitionCount;
    ULONG CacheTransitionCount;
    ULONG DemandZeroCount;
    ULONG PageReadCount;
    ULONG PageReadIoCount;
    ULONG CacheReadCount;
    ULONG CacheIoCount;
    ULONG DirtyPagesWriteCount;
    ULONG DirtyWriteIoCount;
    ULONG MappedPagesWriteCount;
    ULONG MappedWriteIoCount;
    ULONG PagedPoolPages;
    ULONG NonPagedPoolPages;
    ULONG PagedPoolAllocs;
    ULONG PagedPoolFrees;
    ULONG NonPagedPoolAllocs;
    ULONG NonPagedPoolFrees;
    ULONG FreeSystemPtes;
    ULONG ResidentSystemCodePage;
    ULONG TotalSystemDriverPages;
    ULONG TotalSystemCodePages;
    ULONG NonPagedPoolLookasideHits;
    ULONG PagedPoolLookasideHits;
    ULONG AvailablePagedPoolPages;
    ULONG ResidentSystemCachePage;
    ULONG ResidentPagedPoolPage;
    ULONG ResidentSystemDriverPage;
    ULONG CcFastReadNoWait;
    ULONG CcFastReadWait;
    ULONG CcFastReadResourceMiss;
    ULONG CcFastReadNotPossible;
    ULONG CcFastMdlReadNoWait;
    ULONG CcFastMdlReadWait;
    ULONG CcFastMdlReadResourceMiss;
    ULONG CcFastMdlReadNotPossible;
    ULONG CcMapDataNoWait;
    ULONG CcMapDataWait;
    ULONG CcMapDataNoWaitMiss;
    ULONG CcMapDataWaitMiss;
    ULONG CcPinMappedDataCount;
    ULONG CcPinReadNoWait;
    ULONG CcPinReadWait;
    ULONG CcPinReadNoWaitMiss;
    ULONG CcPinReadWaitMiss;
    ULONG CcCopyReadNoWait;
    ULONG CcCopyReadWait;
    ULONG CcCopyReadNoWaitMiss;
    ULONG CcCopyReadWaitMiss;
} SYSTEM_PERF_INFO;

typedef struct {
    SIZE_T ZeroPageCount;
    SIZE_T FreePageCount;
    SIZE_T ModifiedPageCount;
    SIZE_T ModifiedNoWritePageCount;
    SIZE_T BadPageCount;
    SIZE_T PageCountByPriority[8];   // Standby by priority 0–7
    SIZE_T RepurposedPagesByPriority[8];
    SIZE_T ModifiedPageCountPageFile;
} SYSTEM_MEMORY_LIST_INFORMATION;

typedef NTSTATUS (NTAPI *PfnNtQSI)(ULONG, PVOID, ULONG, PULONG);

namespace Sensors {

static PfnNtQSI s_NtQSI       = nullptr;
static DWORD    s_page_size    = 4096;
static uint64_t s_prev_pfaults = 0;
static bool     s_initialized  = false;

bool MemInit() {
    if (s_initialized) return true;
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return false;
    s_NtQSI = reinterpret_cast<PfnNtQSI>(GetProcAddress(ntdll, "NtQuerySystemInformation"));

    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    s_page_size = si.dwPageSize;

    s_initialized = true;
    return true;
}

bool MemPoll(MemSnapshot& snap, double elapsed_sec) {
    constexpr double GB = 1024.0 * 1024.0 * 1024.0;

    // Standard memory status
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) return false;

    snap.total_gb     = static_cast<double>(ms.ullTotalPhys)      / GB;
    snap.available_gb = static_cast<double>(ms.ullAvailPhys)       / GB;
    snap.used_gb      = snap.total_gb - snap.available_gb;
    snap.percent      = ms.dwMemoryLoad;

    // Page file (virtual commit)
    snap.commit_total_gb = static_cast<double>(ms.ullTotalPageFile) / GB;
    snap.commit_used_gb  = snap.commit_total_gb
                         - static_cast<double>(ms.ullAvailPageFile) / GB;

    // Swap is page file minus RAM (simple approximation)
    snap.swap_total_gb = (ms.ullTotalPageFile > ms.ullTotalPhys)
        ? static_cast<double>(ms.ullTotalPageFile - ms.ullTotalPhys) / GB : 0.0;
    snap.swap_used_gb  = (snap.commit_used_gb > snap.used_gb)
        ? snap.commit_used_gb - snap.used_gb : 0.0;
    snap.swap_percent  = (snap.swap_total_gb > 0)
        ? snap.swap_used_gb / snap.swap_total_gb * 100.0 : 0.0;

    // Standby / modified page lists via NT
    if (s_NtQSI) {
        SYSTEM_MEMORY_LIST_INFORMATION mli{};
        ULONG ret = 0;
        if (s_NtQSI(SystemMemoryListInformation, &mli, sizeof(mli), &ret) == 0) {
            SIZE_T standby = 0;
            for (int i = 0; i < 8; ++i) standby += mli.PageCountByPriority[i];
            snap.standby_gb  = static_cast<double>(standby)                  * s_page_size / GB;
            snap.modified_gb = static_cast<double>(mli.ModifiedPageCount)    * s_page_size / GB;
        }
    }

    // Page fault rate via NtQuerySystemInformation(SystemPerformanceInformation)
    if (s_NtQSI) {
        SYSTEM_PERF_INFO spi{};
        ULONG ret = 0;
        if (s_NtQSI(SystemPerformanceInformation, &spi, sizeof(spi), &ret) == 0) {
            uint64_t pf_now = static_cast<uint64_t>(spi.PageFaultCount);
            if (s_prev_pfaults > 0 && elapsed_sec > 0.0)
                snap.page_fault_rate = static_cast<double>(pf_now - s_prev_pfaults) / elapsed_sec;
            s_prev_pfaults = pf_now;
        }
    }

    return true;
}

void MemShutdown() { s_initialized = false; }

} // namespace Sensors
