#pragma once
#include <string>
#include <vector>

namespace Sensors {

struct DiskDevice {
    std::string name;       // e.g. "PhysicalDrive0"
    double read_bytes_s;
    double write_bytes_s;
    double read_iops;
    double write_iops;
    double busy_pct;
};

struct DiskMountpoint {
    std::string path;       // e.g. "C:\"
    std::string device;
    std::string fstype;
    double total_gb;
    double used_gb;
    double free_gb;
    double used_pct;
};

struct DiskSnapshot {
    std::vector<DiskDevice>     devices;
    std::vector<DiskMountpoint> mounts;
};

bool DiskInit();
bool DiskPoll(DiskSnapshot& snap, double elapsed_sec);
void DiskShutdown();

} // namespace Sensors
