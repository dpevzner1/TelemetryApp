#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <string>
#include "fleet_heartbeat.h"
#include "enterprise_config.h"
#include "diagnostic_log.h"
#include "../shared/shm_layout.h"

using json = nlohmann::json;

namespace Service {

static std::string Trim(std::string s) {
    auto ws = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!s.empty() && ws(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && ws(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

static bool SplitUrl(const std::string& url, std::string& host, int& port) {
    std::string u = Trim(url);
    if (u.empty()) return false;
    const std::string http = "http://";
    const std::string https = "https://";
    if (u.rfind(http, 0) == 0) {
        u = u.substr(http.size());
        port = 80;
    } else if (u.rfind(https, 0) == 0) {
        u = u.substr(https.size());
        port = 443;
    } else {
        port = HTTP_PORT;
    }
    size_t slash = u.find('/');
    if (slash != std::string::npos) u = u.substr(0, slash);
    size_t colon = u.rfind(':');
    if (colon != std::string::npos && colon + 1 < u.size()) {
        host = u.substr(0, colon);
        try { port = std::clamp(std::stoi(u.substr(colon + 1)), 1, 65535); }
        catch (...) { port = HTTP_PORT; }
    } else {
        host = u;
    }
    host = Trim(host);
    return !host.empty();
}

void FleetHeartbeatRun(std::atomic<bool>& stop) {
    int attempt = 0;
    while (!stop.load(std::memory_order_acquire)) {
        const auto& cfg = GetEnterpriseConfig();
        if (cfg.install_mode != "FleetHost" && !cfg.host_url.empty()) {
            std::string host;
            int port = HTTP_PORT;
            if (SplitUrl(cfg.host_url, host, port)) {
                json body = EnterpriseConfigJson(true);
                body["api_port"] = HTTP_PORT;
                body["heartbeat_interval_sec"] = 30;
                body["observed_address_policy"] = "host_uses_tcp_remote_addr";

                httplib::Client cli(host, port);
                cli.set_connection_timeout(2, 0);
                cli.set_read_timeout(4, 0);
                cli.set_write_timeout(2, 0);
                auto res = cli.Post("/api/v1/fleet/heartbeat", body.dump(), "application/json");
                if (res && res->status >= 200 && res->status < 300) {
                    if ((attempt % 20) == 0) {
                        DiagnosticLogInfo("Fleet heartbeat accepted by " + host + ":" + std::to_string(port));
                    }
                } else if ((attempt % 6) == 0) {
                    std::string code = res ? std::to_string(res->status) : "no response";
                    DiagnosticLogWarn("Fleet heartbeat failed for " + host + ":" + std::to_string(port) + " (" + code + ")");
                }
                ++attempt;
            }
        }
        for (int i = 0; i < 30 && !stop.load(std::memory_order_acquire); ++i) Sleep(1000);
    }
}

} // namespace Service
