#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include "app_config.h"

using json = nlohmann::json;

namespace Client {

static AppConfig s_instance;

AppConfig& AppConfig::Instance() { return s_instance; }

static std::string ReadInstallRegStr(const char* value) {
    const REGSAM views[] = { KEY_WOW64_64KEY, KEY_WOW64_32KEY, 0 };
    for (REGSAM view : views) {
        HKEY hk{};
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\TelemetryApp", 0,
                          KEY_READ | view, &hk) != ERROR_SUCCESS) {
            continue;
        }
        char buf[512]{};
        DWORD type = 0;
        DWORD len = sizeof(buf);
        std::string out;
        if (RegQueryValueExA(hk, value, nullptr, &type,
                             reinterpret_cast<LPBYTE>(buf), &len) == ERROR_SUCCESS &&
            (type == REG_SZ || type == REG_EXPAND_SZ) && len > 1) {
            out.assign(buf, strnlen(buf, sizeof(buf)));
        }
        RegCloseKey(hk);
        if (!out.empty()) return out;
    }
    return {};
}

void AppConfig::ResolveFromEnv() {
    auto getenv_s = [](const char* name) -> std::string {
        char buf[512]{};
        size_t sz = 0;
        ::getenv_s(&sz, buf, sizeof(buf), name);
        return std::string(buf, sz > 0 ? sz - 1 : 0);
    };

    std::string app_dir  = getenv_s("TELEMETRY_APP_DIR");
    std::string data_dir = getenv_s("TELEMETRY_DATA_DIR");
    std::string api_url  = getenv_s("TELEMETRY_API_URL");
    std::string mode     = getenv_s("TELEMETRY_INSTALL_MODE");
    if (mode.empty()) mode = getenv_s("TELEMETRY_INSTALL_ROLE");
    if (mode.empty()) mode = ReadInstallRegStr("InstallMode");
    if (mode.empty()) mode = ReadInstallRegStr("InstallRole");

    if (!app_dir.empty())  install_dir = app_dir;
    if (!mode.empty())     install_mode = mode;
    if (!data_dir.empty()) {
        this->data_dir    = data_dir;
        dashboards_dir    = data_dir + "\\dashboards";
        log_dir           = data_dir + "\\logs";
    }
    if (!api_url.empty()) this->api_url = api_url;

    // Fallback: derive from executable path
    if (install_dir.empty()) {
        char path[MAX_PATH]{};
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        std::string p = path;
        auto pos = p.find_last_of("\\/");
        if (pos != std::string::npos) install_dir = p.substr(0, pos);
    }
    if (data_dir.empty()) {
        this->data_dir = install_dir + "\\data";
        dashboards_dir  = install_dir + "\\dashboards";
        log_dir         = install_dir + "\\logs";
    }
}

bool AppConfig::Load(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    try {
        json j = json::parse(f);
        if (j.contains("api_url"))     api_url     = j["api_url"];
        if (j.contains("api_key"))     api_key     = j["api_key"];
        if (j.contains("api_port"))    api_port    = j["api_port"];
        if (j.contains("theme"))       theme       = j["theme"];
        if (j.contains("poll_interval_ms")) poll_interval_ms = j["poll_interval_ms"];
        if (j.contains("start_minimized"))  start_minimized  = j["start_minimized"];
        if (j.contains("start_with_service")) start_with_service = j["start_with_service"];
        if (j.contains("logging_enabled"))  logging_enabled  = j["logging_enabled"];
        if (j.contains("log_interval_sec")) log_interval_sec = j["log_interval_sec"];
        if (j.contains("log_format"))       log_format       = j["log_format"];
        if (j.contains("log_metric_set"))   log_metric_set   = j["log_metric_set"];
        if (j.contains("log_dir"))          log_dir          = j["log_dir"];
        if (j.contains("active_dashboard_profile")) active_dashboard_profile = j["active_dashboard_profile"];
        if (j.contains("minimize_behavior")) minimize_behavior = (MinimizeBehavior)j["minimize_behavior"].get<int>();
        if (j.contains("hud_position"))      hud_position      = (HudPositionCfg)j["hud_position"].get<int>();
        if (j.contains("hud_metric_ids")) {
            hud_metric_ids.clear();
            for (auto& v : j["hud_metric_ids"]) hud_metric_ids.push_back(v.get<uint32_t>());
        }
    } catch (...) { return false; }
    return true;
}

bool AppConfig::Save(const std::string& path) const {
    json j;
    j["api_url"]                   = api_url;
    j["api_key"]                   = api_key;
    j["api_port"]                  = api_port;
    j["theme"]                     = theme;
    j["poll_interval_ms"]          = poll_interval_ms;
    j["start_minimized"]           = start_minimized;
    j["start_with_service"]        = start_with_service;
    j["logging_enabled"]           = logging_enabled;
    j["log_interval_sec"]          = log_interval_sec;
    j["log_format"]                = log_format;
    j["log_metric_set"]            = log_metric_set;
    j["log_dir"]                   = log_dir;
    j["active_dashboard_profile"]  = active_dashboard_profile;
    j["minimize_behavior"]         = (int)minimize_behavior;
    j["hud_position"]              = (int)hud_position;
    j["hud_metric_ids"]            = hud_metric_ids;
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << j.dump(2);
    return true;
}

} // namespace Client
