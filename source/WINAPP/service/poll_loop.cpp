#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#include <thread>
#include <chrono>
#include <vector>
#include <cstring>
#include <algorithm>

#include "poll_loop.h"
#include "service_control.h"
#include "diagnostic_log.h"
#include "validation.h"
#include "log_session.h"
#include "sensors/cpu.h"
#include "sensors/memory.h"
#include "sensors/gpu.h"
#include "sensors/disk.h"
#include "sensors/network.h"
#include "sensors/temperatures.h"
#include "sensors/pdh_queries.h"
#include "process_watcher/watcher.h"
#include "ipc/shm_writer.h"
#include "../shared/shm_layout.h"
#include "../shared/metric_ids.h"

using namespace Sensors;
using namespace std::chrono;

namespace Service {

static constexpr int POLL_INTERVAL_MS = 1000;

static PdhQueries s_pdh;

// Get this process's CPU usage for VC-10 self-monitoring
static double GetSelfCpuPct(FILETIME& prev_sys, FILETIME& prev_usr) {
    FILETIME create_t, exit_t, kern_t, user_t;
    if (!GetProcessTimes(GetCurrentProcess(), &create_t, &exit_t, &kern_t, &user_t))
        return 0.0;
    ULARGE_INTEGER sys_now{ kern_t.dwLowDateTime, kern_t.dwHighDateTime };
    ULARGE_INTEGER usr_now{ user_t.dwLowDateTime, user_t.dwHighDateTime };
    ULARGE_INTEGER sys_prev{ prev_sys.dwLowDateTime, prev_sys.dwHighDateTime };
    ULARGE_INTEGER usr_prev{ prev_usr.dwLowDateTime, prev_usr.dwHighDateTime };
    uint64_t delta = (sys_now.QuadPart - sys_prev.QuadPart) +
                     (usr_now.QuadPart - usr_prev.QuadPart);
    prev_sys = kern_t; prev_usr = user_t;
    SYSTEM_INFO si{}; GetSystemInfo(&si);
    // delta is in 100ns ticks; POLL_INTERVAL_MS * 10000 * cores = total 100ns ticks available
    double avail = static_cast<double>(POLL_INTERVAL_MS) * 10000.0 * si.dwNumberOfProcessors;
    return (avail > 0) ? std::min(delta / avail * 100.0, 100.0) : 0.0;
}

bool PollLoopInit() {
    DiagnosticLogInfo("PollLoopInit begin.");
    bool ok = true;
    if (!CpuInit())  { LogEvent(EVENTLOG_WARNING_TYPE, "CPU sensor init failed");  ok = false; }
    else DiagnosticLogInfo("CPU sensor initialized.");
    if (!MemInit())  { LogEvent(EVENTLOG_WARNING_TYPE, "Mem sensor init failed");  ok = false; }
    else DiagnosticLogInfo("Memory sensor initialized.");
    GpuInit(); // optional — PDH fallback if all vendor DLLs absent
    DiagnosticLogInfo("GPU sensor initialization attempted.");
    if (!DiskInit()) { LogEvent(EVENTLOG_WARNING_TYPE, "Disk sensor init failed"); }
    else DiagnosticLogInfo("Disk sensor initialized.");
    if (!NetInit())  { LogEvent(EVENTLOG_WARNING_TYPE, "Net sensor init failed");  }
    else DiagnosticLogInfo("Network sensor initialized.");
    TempInit();
    DiagnosticLogInfo("Temperature sensor initialization attempted.");
    if (!PdhInit(s_pdh)) LogEvent(EVENTLOG_WARNING_TYPE, "PDH GPU counter init failed");
    else DiagnosticLogInfo("PDH GPU counters initialized.");

    // Ensure SHM is created before the first poll
    if (!ShmOpen()) { LogEvent(EVENTLOG_ERROR_TYPE, "SHM create failed"); return false; }
    DiagnosticLogInfo("Shared memory opened.");
    DiagnosticLogInfo(ok ? "PollLoopInit complete." : "PollLoopInit complete with degraded required sensors.");
    return ok;
}

void PollLoopRun(std::atomic<bool>& stop) {
    DiagnosticLogInfo("PollLoopRun started.");
    FILETIME ft_sys{}, ft_usr{};
    FILETIME ft_create{}, ft_exit{};
    GetProcessTimes(GetCurrentProcess(), &ft_create, &ft_exit, &ft_sys, &ft_usr);

    auto next_tick = steady_clock::now() + milliseconds(POLL_INTERVAL_MS);
    auto prev_tick = steady_clock::now() - milliseconds(POLL_INTERVAL_MS);

    uint64_t tick_count = 0;
    while (!stop.load(std::memory_order_acquire)) {
        ++tick_count;
        auto poll_start = steady_clock::now();
        double elapsed_sec = duration<double>(poll_start - prev_tick).count();
        prev_tick = poll_start;

        // --- PDH: first collect (seeds rate counters) ---
        PdhCollect(s_pdh);
        // Sleep 50ms inside PDH to satisfy rate counter requirement
        std::this_thread::sleep_for(milliseconds(50));
        // Second PDH collect gives valid rates
        PdhCollect(s_pdh);

        // --- GPU engine PDH results ---
        std::unordered_map<uint32_t, PdhGpuResult> phys_gpu;
        std::vector<PidGpuUtil> pid_gpu;
        s_pdh.Collect(phys_gpu, pid_gpu, {}); // process-level requested by watcher separately

        // --- Collect all sensors ---
        CpuSnapshot cpu{};
        CpuPoll(cpu);

        MemSnapshot mem{};
        MemPoll(mem, elapsed_sec);

        std::vector<GpuSnapshot> gpus;
        GpuPoll(gpus);
        // Inject PDH engine util (best available for Intel/AMD if vendor DLL absent)
        for (auto& g : gpus) {
            auto it = phys_gpu.find(static_cast<uint32_t>(g.index));
            if (it != phys_gpu.end()) {
                g.pdh_3d_pct      = it->second.phys_3d_pct;
                g.pdh_compute_pct = it->second.phys_compute_pct;
                // For NVIDIA, usage_pct comes from NVML; for others use PDH 3D
                if (g.vendor != GpuVendor::NVIDIA && g.usage_pct == 0.0)
                    g.usage_pct = g.pdh_3d_pct;
            }
        }

        DiskSnapshot disk{};
        DiskPoll(disk, elapsed_sec);

        std::vector<NicSnapshot> nics;
        NetPoll(nics, elapsed_sec);

        std::vector<TempReading> temps;
        TempPoll(temps);

        GetWatcher().Poll(elapsed_sec);

        double self_cpu = GetSelfCpuPct(ft_sys, ft_usr);

        // --- Validation ---
        std::vector<double> gpu_pcts, disk_busys, temp_vals;
        for (auto& g : gpus) gpu_pcts.push_back(g.usage_pct);
        for (auto& d : disk.devices) disk_busys.push_back(d.busy_pct);
        for (auto& t : temps) temp_vals.push_back(t.celsius);

        auto poll_mid = steady_clock::now();
        double poll_ms_so_far = duration<double>(poll_mid - poll_start).count() * 1000.0;

        ValidationInput vi{};
        vi.cpu_total_pct  = cpu.usage_total_pct;
        vi.mem_pct        = mem.percent;
        vi.gpu_pct        = gpu_pcts.data();
        vi.gpu_count      = static_cast<int>(gpu_pcts.size());
        vi.disk_busy      = disk_busys.data();
        vi.disk_count     = static_cast<int>(disk_busys.size());
        vi.shm_seq_even   = true;  // pre-publish check; seqlock verified in ShmBeginWrite
        vi.per_core_pct   = cpu.per_core_pct;
        vi.core_count     = cpu.core_count;
        vi.poll_ms        = poll_ms_so_far;
        vi.temp_readings  = temp_vals.data();
        vi.temp_count     = static_cast<int>(temp_vals.size());
        vi.self_cpu_pct   = self_cpu;

        auto checks = RunValidations(vi);
        LogFailedValidations(checks);

        // --- Write to SHM ---
        ShmBlock* shm = ShmGet();
        if (shm) {
            ShmBeginWrite(shm);

            shm->hdr.ts_poll_ms       = static_cast<uint64_t>(
                duration_cast<milliseconds>(poll_start.time_since_epoch()).count());
            shm->hdr.active_gpu_count  = static_cast<uint32_t>(gpus.size());
            shm->hdr.active_disk_count = static_cast<uint32_t>(disk.devices.size());
            shm->hdr.active_nic_count  = static_cast<uint32_t>(nics.size());
            shm->hdr.active_temp_count = static_cast<uint32_t>(temps.size());
            shm->hdr.active_cpu_cores  = static_cast<uint32_t>(cpu.core_count);
            shm->hdr.active_watch_count = 0;
            shm->hdr.service_alive     = 1;

            // CPU
            ShmPush(shm, MetricId::CPU_USAGE_TOTAL,    cpu.usage_total_pct);
            ShmPush(shm, MetricId::CPU_FREQ_ACTUAL_MHZ,cpu.freq_actual_mhz);
            for (int i = 0; i < cpu.core_count && i < 32; ++i)
                ShmPush(shm, cpu_core_metric(i), cpu.per_core_pct[i]);

            // Memory
            ShmPush(shm, MetricId::MEM_TOTAL_GB,    mem.total_gb);
            ShmPush(shm, MetricId::MEM_USED_GB,     mem.used_gb);
            ShmPush(shm, MetricId::MEM_AVAILABLE_GB, mem.available_gb);
            ShmPush(shm, MetricId::MEM_PERCENT,      mem.percent);
            ShmPush(shm, MetricId::MEM_SWAP_USED_GB, mem.swap_used_gb);
            ShmPush(shm, MetricId::MEM_SWAP_PERCENT, mem.swap_percent);
            ShmPush(shm, MetricId::MEM_STANDBY_GB,   mem.standby_gb);
            ShmPush(shm, MetricId::MEM_PAGE_FAULT_RATE, mem.page_fault_rate);

            // GPUs
            for (int gi = 0; gi < static_cast<int>(gpus.size()) && gi < 4; ++gi) {
                const auto& g = gpus[gi];
                ShmPush(shm, gpu_metric(gi, GpuOff::USAGE_PCT),      g.usage_pct);
                ShmPush(shm, gpu_metric(gi, GpuOff::VRAM_USED_MB),   g.vram_used_mb);
                ShmPush(shm, gpu_metric(gi, GpuOff::VRAM_TOTAL_MB),  g.vram_total_mb);
                ShmPush(shm, gpu_metric(gi, GpuOff::VRAM_PCT),       g.vram_percent);
                ShmPush(shm, gpu_metric(gi, GpuOff::TEMP_C),         g.temp_celsius);
                ShmPush(shm, gpu_metric(gi, GpuOff::POWER_W),        g.power_watts);
                ShmPush(shm, gpu_metric(gi, GpuOff::FAN_PCT),        g.fan_pct);
                ShmPush(shm, gpu_metric(gi, GpuOff::CLOCK_CORE_MHZ), g.clock_core_mhz);
                ShmPush(shm, gpu_metric(gi, GpuOff::CLOCK_MEM_MHZ),  g.clock_mem_mhz);
                ShmPush(shm, gpu_metric(gi, GpuOff::PDH_UTIL_PCT),   g.pdh_3d_pct);
                ShmPush(shm, gpu_metric(gi, GpuOff::ENCODER_PCT),    g.encoder_pct);
                ShmPush(shm, gpu_metric(gi, GpuOff::DECODER_PCT),    g.decoder_pct);
                ShmPush(shm, gpu_metric(gi, GpuOff::SM_UTIL_PCT),     g.sm_util_pct);
                ShmPush(shm, gpu_metric(gi, GpuOff::MEM_BW_UTIL_PCT), g.mem_bw_util_pct);
                ShmPush(shm, gpu_metric(gi, GpuOff::MEM_CLK_TRANSITIONS), g.mem_clk_transitions);
                ShmPush(shm, gpu_metric(gi, GpuOff::TENSOR_ACTIVE),   g.has_tensor_cores ? 1.0 : 0.0);
                ShmPush(shm, gpu_metric(gi, GpuOff::CUDA_CC_MAJOR),   g.cuda_cc_major);
                ShmPush(shm, gpu_metric(gi, GpuOff::CUDA_CC_MINOR),   g.cuda_cc_minor);
                ShmPush(shm, gpu_metric(gi, GpuOff::TENSOR_CORE_GEN), g.tensor_core_gen);
                // Label — char[] field, GPU names are ASCII/UTF-8
                strncpy(shm->hdr.gpu_name[gi], g.name.c_str(), SHM_GPU_NAME_LEN - 1);
            }

            // Disks
            for (int di = 0; di < static_cast<int>(disk.devices.size()) && di < 8; ++di) {
                const auto& d = disk.devices[di];
                ShmPush(shm, disk_metric(di, DiskOff::READ_BYTES_S),  d.read_bytes_s);
                ShmPush(shm, disk_metric(di, DiskOff::WRITE_BYTES_S), d.write_bytes_s);
                ShmPush(shm, disk_metric(di, DiskOff::READ_IOPS),     d.read_iops);
                ShmPush(shm, disk_metric(di, DiskOff::WRITE_IOPS),    d.write_iops);
                ShmPush(shm, disk_metric(di, DiskOff::BUSY_PCT),      d.busy_pct);
                strncpy(shm->hdr.disk_name[di], d.name.c_str(), SHM_DISK_NAME_LEN - 1);
            }

            // NICs
            for (int ni = 0; ni < static_cast<int>(nics.size()) && ni < 8; ++ni) {
                const auto& n = nics[ni];
                ShmPush(shm, net_metric(ni, NetOff::RECV_BYTES_S), n.recv_bytes_s);
                ShmPush(shm, net_metric(ni, NetOff::SENT_BYTES_S), n.sent_bytes_s);
                ShmPush(shm, net_metric(ni, NetOff::RECV_PKTS_S),  n.recv_pkts_s);
                ShmPush(shm, net_metric(ni, NetOff::SENT_PKTS_S),  n.sent_pkts_s);
                strncpy(shm->hdr.nic_name[ni], n.name.c_str(), SHM_NIC_NAME_LEN - 1);
            }

            // Temperatures
            for (int ti = 0; ti < static_cast<int>(temps.size()) && ti < 32; ++ti) {
                ShmPush(shm, temp_metric(ti), temps[ti].celsius);
                strncpy(shm->hdr.temp_name[ti], temps[ti].name.c_str(), SHM_TEMP_NAME_LEN - 1);
            }

            // Self-monitoring
            ShmPush(shm, MetricId::SELF_CPU_PCT, self_cpu);

            // Watched processes
            GetWatcher().WriteToShm();

            auto poll_end = steady_clock::now();
            shm->hdr.poll_duration_ms = static_cast<uint32_t>(
                duration_cast<milliseconds>(poll_end - poll_start).count());

            ShmEndWrite(shm);

            // Write one JSONL row per active logging session
            LogSessionStore::Instance().OnPollTick(shm);

            if (tick_count <= 10 || (tick_count % 60) == 0) {
                DiagnosticLogInfo(
                    "Poll tick " + std::to_string(tick_count) +
                    ": duration_ms=" + std::to_string(shm->hdr.poll_duration_ms) +
                    ", cpu_pct=" + std::to_string(cpu.usage_total_pct) +
                    ", mem_pct=" + std::to_string(mem.percent) +
                    ", gpu_count=" + std::to_string(gpus.size()) +
                    ", disk_count=" + std::to_string(disk.devices.size()) +
                    ", nic_count=" + std::to_string(nics.size()) +
                    ", watch_count=" + std::to_string(shm->hdr.active_watch_count) +
                    ", self_cpu_pct=" + std::to_string(self_cpu));
            }
        }

        // Sleep until next tick (accounts for time already spent)
        auto now = steady_clock::now();
        if (now < next_tick)
            std::this_thread::sleep_for(next_tick - now);
        next_tick += milliseconds(POLL_INTERVAL_MS);
    }
    DiagnosticLogInfo("PollLoopRun stopped.");
}

void PollLoopShutdown() {
    DiagnosticLogInfo("PollLoopShutdown begin.");
    PdhShutdown(s_pdh);
    CpuShutdown();
    MemShutdown();
    GpuShutdown();
    DiskShutdown();
    NetShutdown();
    TempShutdown();
    ShmClose();
    DiagnosticLogInfo("PollLoopShutdown complete.");
}

} // namespace Service
