#pragma once
#include <string>
#include <vector>

namespace Sensors {

struct NicSnapshot {
    std::string name;
    std::string description;
    double recv_bytes_s;
    double sent_bytes_s;
    double recv_pkts_s;
    double sent_pkts_s;
    uint64_t errors_in;
    uint64_t errors_out;
    uint64_t drops_in;
    uint64_t drops_out;
    bool     is_up;
};

bool NetInit();
bool NetPoll(std::vector<NicSnapshot>& snaps, double elapsed_sec);
void NetShutdown();

} // namespace Sensors
