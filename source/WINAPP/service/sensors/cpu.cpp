#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <pdh.h>
#include <intrin.h>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include "cpu.h"

#pragma comment(lib, "pdh.lib")

// NtQuerySystemInformation is declared in winternl.h but the full
// SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION struct is not exported there.
// We declare it manually.
#ifndef SystemProcessorPerformanceInformation
#define SystemProcessorPerformanceInformation 8
#endif

typedef struct _SYSTEM_PROCESSOR_PERF_INFO {
    LARGE_INTEGER IdleTime;
    LARGE_INTEGER KernelTime;   // KernelTime includes IdleTime
    LARGE_INTEGER UserTime;
    LARGE_INTEGER DpcTime;
    LARGE_INTEGER InterruptTime;
    ULONG InterruptCount;
} SYSTEM_PROCESSOR_PERF_INFO;

typedef NTSTATUS (NTAPI *PfnNtQuerySysInfo)(
    ULONG  InfoClass,
    PVOID  Info,
    ULONG  InfoLen,
    PULONG ReturnLen
);

namespace Sensors {

static PfnNtQuerySysInfo s_NtQSI       = nullptr;
static SYSTEM_PROCESSOR_PERF_INFO s_prev[32] = {};
static int    s_core_count              = 0;
static double s_rated_max_mhz           = 0.0;
static bool   s_initialized             = false;

// PDH handle for % Processor Performance (actual boost frequency ratio)
static HQUERY  s_pdh_query              = nullptr;
static HCOUNTER s_pdh_freq_counter      = nullptr;
static bool    s_pdh_ready              = false;

static bool InitPdh() {
    if (PdhOpenQuery(nullptr, 0, &s_pdh_query) != ERROR_SUCCESS) return false;
    const wchar_t* path = L"\\Processor Information(_Total)\\% Processor Performance";
    if (PdhAddCounterW(s_pdh_query, path, 0, &s_pdh_freq_counter) != ERROR_SUCCESS) {
        PdhCloseQuery(s_pdh_query);
        s_pdh_query = nullptr;
        return false;
    }
    // Seed first sample (PDH requires two collects for a valid rate)
    PdhCollectQueryData(s_pdh_query);
    return true;
}

static double QueryActualFreqMhz() {
    if (!s_pdh_ready || !s_pdh_query) return 0.0;
    PdhCollectQueryData(s_pdh_query);
    PDH_FMT_COUNTERVALUE val{};
    if (PdhGetFormattedCounterValue(s_pdh_freq_counter,
                                    PDH_FMT_DOUBLE, nullptr, &val) != ERROR_SUCCESS)
        return 0.0;
    return val.doubleValue / 100.0 * s_rated_max_mhz;
}

bool CpuInit() {
    if (s_initialized) return true;

    // Resolve NtQuerySystemInformation
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return false;
    s_NtQSI = reinterpret_cast<PfnNtQuerySysInfo>(
        GetProcAddress(ntdll, "NtQuerySystemInformation"));
    if (!s_NtQSI) return false;

    // Core count
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    s_core_count = static_cast<int>(si.dwNumberOfProcessors);
    if (s_core_count < 1) s_core_count = 1;
    if (s_core_count > 32) s_core_count = 32;

    // Rated max frequency from registry
    HKEY hk{};
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
            L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
            0, KEY_READ, &hk) == ERROR_SUCCESS) {
        DWORD mhz = 0, sz = sizeof(mhz);
        RegQueryValueExW(hk, L"~MHz", nullptr, nullptr,
                         reinterpret_cast<LPBYTE>(&mhz), &sz);
        RegCloseKey(hk);
        s_rated_max_mhz = static_cast<double>(mhz);
    }

    // Seed prev sample
    ULONG needed = 0;
    s_NtQSI(SystemProcessorPerformanceInformation,
             s_prev, sizeof(s_prev), &needed);

    // Init PDH for freq measurement
    s_pdh_ready = InitPdh();

    s_initialized = true;
    return true;
}

bool CpuPoll(CpuSnapshot& snap) {
    if (!s_NtQSI) return false;

    SYSTEM_PROCESSOR_PERF_INFO cur[32]{};
    ULONG needed = 0;
    NTSTATUS st = s_NtQSI(SystemProcessorPerformanceInformation,
                           cur, static_cast<ULONG>(sizeof(cur)), &needed);
    if (st != 0) return false;

    int cores = s_core_count;
    double total_active = 0.0;
    double total_elapsed = 0.0;
    int64_t ctx = 0, intr = 0;

    for (int i = 0; i < cores; ++i) {
        int64_t idle_d   = cur[i].IdleTime.QuadPart    - s_prev[i].IdleTime.QuadPart;
        int64_t kernel_d = cur[i].KernelTime.QuadPart  - s_prev[i].KernelTime.QuadPart;
        int64_t user_d   = cur[i].UserTime.QuadPart    - s_prev[i].UserTime.QuadPart;
        // KernelTime includes IdleTime on Windows
        int64_t elapsed  = kernel_d + user_d;
        double  usage    = (elapsed > 0)
            ? std::max(0.0, std::min(100.0, (1.0 - (double)idle_d / elapsed) * 100.0))
            : 0.0;

        snap.per_core_pct[i] = usage;
        total_active  += (elapsed - idle_d);
        total_elapsed += elapsed;

        ctx  += cur[i].InterruptCount - s_prev[i].InterruptCount;  // InterruptCount proxy
    }

    snap.usage_total_pct = (total_elapsed > 0)
        ? std::max(0.0, std::min(100.0, (1.0 - (double)(total_elapsed - total_active) / total_elapsed) * 100.0))
        : 0.0;
    snap.core_count = cores;

    // Core balance analysis
    double max_core = *std::max_element(snap.per_core_pct, snap.per_core_pct + cores);
    double sum_core = 0.0;
    int cold = 0, hot_idx = 0;
    for (int i = 0; i < cores; ++i) {
        sum_core += snap.per_core_pct[i];
        if (snap.per_core_pct[i] < 5.0) ++cold;
        if (snap.per_core_pct[i] == max_core) hot_idx = i;
    }
    snap.hot_core_index     = hot_idx;
    snap.hot_core_pct       = max_core;
    snap.cold_cores_count   = cold;
    snap.core_balance_score = (max_core > 0.0) ? (sum_core / cores) / max_core : 1.0;

    // Frequency
    snap.freq_rated_max_mhz = s_rated_max_mhz;
    snap.freq_actual_mhz    = QueryActualFreqMhz();

    // Context switches / interrupts (from KernelTime delta as proxy; full stats
    // need NtQuerySystemInformation(SystemPerformanceInformation) class 2)
    snap.ctx_switches_total = ctx;
    snap.interrupts_total   = intr;

    std::memcpy(s_prev, cur, sizeof(s_prev));
    return true;
}

void CpuShutdown() {
    if (s_pdh_query) { PdhCloseQuery(s_pdh_query); s_pdh_query = nullptr; }
    s_initialized = false;
}

} // namespace Sensors
