#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace Client {

enum class MinimizeBehavior : int {
    Normal    = 0,  // standard Windows minimize
    HUDMode   = 1,  // compact overlay bar (default)
    SystemTray = 2, // collapse to tray icon
};

enum class HudPositionCfg : int {
    AboveTaskbar = 0,
    Top          = 1,
    Left         = 2,
    Right        = 3,
};

struct AppConfig {
    // Service connection
    std::string api_url       = "http://localhost:8765";
    std::string api_key;
    int         api_port      = 8765;

    // Paths (resolved at startup from env vars or defaults)
    std::string install_dir;
    std::string data_dir;
    std::string dashboards_dir;
    std::string log_dir;
    std::string install_mode = "LocalMonitor";

    // Display
    std::string  theme          = "dark";
    int          poll_interval_ms = 1000;
    bool         start_minimized  = false;
    bool         start_with_service = true;
    std::string  active_dashboard_profile = "Default";

    // HUD / minimize behavior
    MinimizeBehavior  minimize_behavior = MinimizeBehavior::HUDMode;
    HudPositionCfg    hud_position      = HudPositionCfg::AboveTaskbar;
    std::vector<uint32_t> hud_metric_ids; // empty = use MakeDefaultHudMetrics()

    // Logging
    bool         logging_enabled    = false;
    int          log_interval_sec   = 1;
    std::string  log_format         = "jsonl";
    std::string  log_metric_set     = "all";

    bool Load(const std::string& path);
    bool Save(const std::string& path) const;

    static AppConfig& Instance();
    void ResolveFromEnv();
};

} // namespace Client
