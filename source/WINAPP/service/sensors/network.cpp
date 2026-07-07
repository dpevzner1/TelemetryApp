#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <vector>
#include <string>
#include <algorithm>
#include "network.h"

#pragma comment(lib, "iphlpapi.lib")

namespace Sensors {

struct NicPrev {
    IF_INDEX  index;
    uint64_t  bytes_in;
    uint64_t  bytes_out;
    uint64_t  pkts_in;
    uint64_t  pkts_out;
};

static std::vector<NicPrev> s_prev;
static bool s_initialized = false;

bool NetInit() {
    if (s_initialized) return true;
    // Seed with current counters
    MIB_IF_TABLE2* tbl = nullptr;
    if (GetIfTable2(&tbl) == NO_ERROR) {
        for (DWORD i = 0; i < tbl->NumEntries; ++i) {
            auto& e = tbl->Table[i];
            if (e.Type != IF_TYPE_ETHERNET_CSMACD && e.Type != IF_TYPE_IEEE80211) continue;
            s_prev.push_back({(IF_INDEX)e.InterfaceIndex,
                e.InOctets, e.OutOctets, e.InUcastPkts, e.OutUcastPkts});
        }
        FreeMibTable(tbl);
    }
    s_initialized = true;
    return true;
}

bool NetPoll(std::vector<NicSnapshot>& snaps, double elapsed_sec) {
    if (elapsed_sec <= 0.0) elapsed_sec = 1.0;
    snaps.clear();

    MIB_IF_TABLE2* tbl = nullptr;
    if (GetIfTable2(&tbl) != NO_ERROR) return false;

    for (DWORD i = 0; i < tbl->NumEntries; ++i) {
        auto& e = tbl->Table[i];
        if (e.Type != IF_TYPE_ETHERNET_CSMACD && e.Type != IF_TYPE_IEEE80211) continue;
        if (!e.InterfaceAndOperStatusFlags.FilterInterface) { /* skip virtual */ }

        NicPrev* prev = nullptr;
        for (auto& p : s_prev) if (p.index == e.InterfaceIndex) { prev = &p; break; }
        if (!prev) {
            s_prev.push_back({(IF_INDEX)e.InterfaceIndex,
                e.InOctets, e.OutOctets, e.InUcastPkts, e.OutUcastPkts});
            prev = &s_prev.back();
        }

        NicSnapshot n{};
        char name_a[256]{};
        WideCharToMultiByte(CP_UTF8, 0, e.Alias, -1, name_a, sizeof(name_a), nullptr, nullptr);
        n.name = name_a;
        char desc_a[256]{};
        WideCharToMultiByte(CP_UTF8, 0, e.Description, -1, desc_a, sizeof(desc_a), nullptr, nullptr);
        n.description  = desc_a;
        n.is_up        = (e.OperStatus == IfOperStatusUp);
        n.recv_bytes_s = (e.InOctets    - prev->bytes_in)  / elapsed_sec;
        n.sent_bytes_s = (e.OutOctets   - prev->bytes_out) / elapsed_sec;
        n.recv_pkts_s  = (e.InUcastPkts - prev->pkts_in)   / elapsed_sec;
        n.sent_pkts_s  = (e.OutUcastPkts- prev->pkts_out)  / elapsed_sec;
        n.errors_in    = e.InErrors;
        n.errors_out   = e.OutErrors;
        n.drops_in     = e.InDiscards;
        n.drops_out    = e.OutDiscards;

        prev->bytes_in  = e.InOctets;
        prev->bytes_out = e.OutOctets;
        prev->pkts_in   = e.InUcastPkts;
        prev->pkts_out  = e.OutUcastPkts;

        if (n.name.empty() || n.name == "Loopback Pseudo-Interface 1") continue;
        snaps.push_back(n);
    }
    FreeMibTable(tbl);
    return true;
}

void NetShutdown() { s_initialized = false; s_prev.clear(); }

} // namespace Sensors
