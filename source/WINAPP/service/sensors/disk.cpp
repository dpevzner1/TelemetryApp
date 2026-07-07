#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <algorithm>
#include <cstring>
#include "disk.h"

namespace Sensors {

struct DiskPrev {
    std::string name;
    LONGLONG    read_bytes;
    LONGLONG    write_bytes;
    LONGLONG    read_count;
    LONGLONG    write_count;
    LONGLONG    busy_ms;
};

static std::vector<DiskPrev> s_prev;
static bool s_initialized = false;

// Enumerate physical disk counters via DeviceIoControl IOCTL_DISK_PERFORMANCE
static bool QueryDiskPerf(const char* dev_name, DISK_PERFORMANCE& perf) {
    char path[64];
    snprintf(path, sizeof(path), "\\\\.\\%s", dev_name);
    HANDLE h = CreateFileA(path, 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD bytes = 0;
    bool ok = !!DeviceIoControl(h, IOCTL_DISK_PERFORMANCE,
        nullptr, 0, &perf, sizeof(perf), &bytes, nullptr);
    CloseHandle(h);
    return ok;
}

bool DiskInit() {
    if (s_initialized) return true;
    // Seed previous sample for each physical drive
    for (int i = 0; i < 16; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "PhysicalDrive%d", i);
        DISK_PERFORMANCE perf{};
        if (!QueryDiskPerf(name, perf)) break;
        s_prev.push_back({name,
            perf.BytesRead.QuadPart, perf.BytesWritten.QuadPart,
            perf.ReadCount, perf.WriteCount, perf.QueryTime.QuadPart / 10000LL});
    }
    s_initialized = true;
    return true;
}

bool DiskPoll(DiskSnapshot& snap, double elapsed_sec) {
    snap.devices.clear();
    snap.mounts.clear();
    if (elapsed_sec <= 0.0) elapsed_sec = 1.0;

    // Physical disk I/O deltas
    for (int i = 0; i < 16; ++i) {
        char name[32];
        snprintf(name, sizeof(name), "PhysicalDrive%d", i);
        DISK_PERFORMANCE perf{};
        if (!QueryDiskPerf(name, perf)) break;

        DiskPrev* prev = nullptr;
        for (auto& p : s_prev) if (p.name == name) { prev = &p; break; }
        if (!prev) {
            s_prev.push_back({name,
                perf.BytesRead.QuadPart, perf.BytesWritten.QuadPart,
                perf.ReadCount, perf.WriteCount, perf.QueryTime.QuadPart / 10000LL});
            prev = &s_prev.back();
        }

        LONGLONG busy_ms_now = perf.QueryTime.QuadPart / 10000LL;

        DiskDevice d{};
        d.name           = name;
        d.read_bytes_s   = (perf.BytesRead.QuadPart  - prev->read_bytes)  / elapsed_sec;
        d.write_bytes_s  = (perf.BytesWritten.QuadPart- prev->write_bytes) / elapsed_sec;
        d.read_iops      = (perf.ReadCount            - prev->read_count)  / elapsed_sec;
        d.write_iops     = (perf.WriteCount           - prev->write_count) / elapsed_sec;
        double busy_delta_ms = static_cast<double>(busy_ms_now - prev->busy_ms);
        d.busy_pct       = std::min(busy_delta_ms / (elapsed_sec * 1000.0) * 100.0, 100.0);

        prev->read_bytes  = perf.BytesRead.QuadPart;
        prev->write_bytes = perf.BytesWritten.QuadPart;
        prev->read_count  = perf.ReadCount;
        prev->write_count = perf.WriteCount;
        prev->busy_ms     = busy_ms_now;

        snap.devices.push_back(d);
    }

    // Volume usage
    wchar_t drives[512]{};
    GetLogicalDriveStringsW(511, drives);
    for (wchar_t* p = drives; *p; p += wcslen(p) + 1) {
        UINT type = GetDriveTypeW(p);
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE) continue;

        ULARGE_INTEGER avail{}, total{}, free{};
        if (!GetDiskFreeSpaceExW(p, &avail, &total, &free)) continue;

        DiskMountpoint m{};
        char path_a[8]{};
        WideCharToMultiByte(CP_UTF8, 0, p, -1, path_a, sizeof(path_a), nullptr, nullptr);
        m.path     = path_a;
        m.total_gb = static_cast<double>(total.QuadPart) / (1024.0 * 1024.0 * 1024.0);
        m.free_gb  = static_cast<double>(free.QuadPart)  / (1024.0 * 1024.0 * 1024.0);
        m.used_gb  = m.total_gb - m.free_gb;
        m.used_pct = m.total_gb > 0 ? m.used_gb / m.total_gb * 100.0 : 0.0;
        snap.mounts.push_back(m);
    }
    return true;
}

void DiskShutdown() { s_initialized = false; s_prev.clear(); }

} // namespace Sensors
