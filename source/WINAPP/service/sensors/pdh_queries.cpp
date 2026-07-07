#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <regex>
#include <algorithm>
#include <cstring>
#include "pdh_queries.h"

namespace Sensors {

bool PdhInit(PdhQueries& q) {
    if (PdhOpenQuery(nullptr, 0, &q.query) != ERROR_SUCCESS) return false;

    // Wildcard counter — enumerates all GPU engine instances including PIDs
    PDH_STATUS st = PdhAddCounterW(
        q.query,
        L"\\GPU Engine(*)\\Utilization Percentage",
        0,
        &q.gpu_engine);

    if (st != ERROR_SUCCESS) {
        PdhCloseQuery(q.query);
        q.query = nullptr;
        return false;
    }

    // Seed first collection (PDH rate counters need two samples)
    PdhCollectQueryData(q.query);
    q.ready = true;
    return true;
}

void PdhCollect(PdhQueries& q) {
    if (q.ready) PdhCollectQueryData(q.query);
}

void PdhQueries::Collect(
    std::unordered_map<uint32_t, PdhGpuResult>& phys_out,
    std::vector<PidGpuUtil>&                     pid_out,
    const std::vector<uint32_t>&                 target_pids)
{
    if (!ready || !gpu_engine) return;

    // Retrieve all formatted counter values from the wildcard counter
    DWORD buf_size = 0, item_count = 0;
    PDH_STATUS st = PdhGetFormattedCounterArrayW(
        gpu_engine, PDH_FMT_DOUBLE, &buf_size, &item_count, nullptr);
    if (st != PDH_MORE_DATA && item_count == 0) return;

    std::vector<BYTE> buf(buf_size);
    auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buf.data());
    st = PdhGetFormattedCounterArrayW(
        gpu_engine, PDH_FMT_DOUBLE, &buf_size, &item_count, items);
    if (st != ERROR_SUCCESS) return;

    // Instance name format: pid_XXXX_luid_..._phys_N_eng_M_engtype_TYPE
    // We parse phys_N (physical GPU index) and pid_XXXX and engtype.
    std::wregex re_phys(L"phys_(\\d+)");
    std::wregex re_pid(L"pid_(\\d+)");

    for (DWORD i = 0; i < item_count; ++i) {
        const wchar_t* inst = items[i].szName;
        double val = items[i].FmtValue.doubleValue;
        if (val <= 0.0) continue;

        bool is_3d      = (wcsstr(inst, L"engtype_3D")      != nullptr);
        bool is_compute = (wcsstr(inst, L"engtype_Compute") != nullptr);
        if (!is_3d && !is_compute) continue;

        // Parse phys index
        std::wcmatch m_phys;
        if (!std::regex_search(inst, m_phys, re_phys)) continue;
        uint32_t phys = static_cast<uint32_t>(std::stoul(m_phys[1].str()));

        // System-wide 3D aggregate
        if (is_3d) {
            auto& r = phys_out[phys];
            r.phys_3d_pct = std::min(r.phys_3d_pct + val, 100.0);
        }
        if (is_compute) {
            auto& r = phys_out[phys];
            r.phys_compute_pct = std::min(r.phys_compute_pct + val, 100.0);
        }

        // Per-process
        if (!target_pids.empty()) {
            std::wcmatch m_pid;
            if (!std::regex_search(inst, m_pid, re_pid)) continue;
            uint32_t pid = static_cast<uint32_t>(std::stoul(m_pid[1].str()));
            bool wanted = std::find(target_pids.begin(), target_pids.end(), pid)
                          != target_pids.end();
            if (!wanted) continue;

            // Find or create entry for this pid
            auto it = std::find_if(pid_out.begin(), pid_out.end(),
                [pid](const PidGpuUtil& e){ return e.pid == pid; });
            if (it == pid_out.end()) {
                pid_out.push_back({pid, 0.0});
                it = pid_out.end() - 1;
            }
            it->util_pct = std::min(it->util_pct + val, 100.0);
        }
    }
}

void PdhShutdown(PdhQueries& q) {
    if (q.query) { PdhCloseQuery(q.query); q.query = nullptr; }
    q.ready = false;
}

} // namespace Sensors
