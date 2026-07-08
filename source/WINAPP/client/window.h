#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>
#include "renderer/d2d_context.h"
#include "ui/sidebar.h"
#include "ui/compact_overlay.h"
#include "ui/system_tray.h"
#include "pages/dashboard_page.h"
#include "pages/api_page.h"
#include "pages/metrics_page.h"
#include "pages/fleet_page.h"
#include "pages/settings_page.h"
#include "config/app_config.h"
#include "config/dashboard_profile.h"
#include "config/metric_catalog_model.h"
#include "ipc/shm_reader.h"

namespace Client {

static constexpr const char* APP_VERSION = "1.0.0";

class AppWindow {
public:
    bool Create(HINSTANCE hinstance);
    void RunMessageLoop();
    void Destroy();

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

private:
    HINSTANCE     m_hinstance  = nullptr;
    HWND          m_hwnd       = nullptr;
    D2DContext    m_d2d;
    std::atomic<bool> m_stop{false};

    // Config and profile
    AppConfig        m_config;
    DashboardProfile m_profile;
    MetricCatalogModel m_metric_catalog;

    // Pages
    Sidebar*        m_sidebar         = nullptr;
    DashboardPage*  m_dashboard_page  = nullptr;
    ApiPage*        m_api_page        = nullptr;
    MetricsPage*    m_metrics_page    = nullptr;
    FleetPage*      m_fleet_page      = nullptr;
    SettingsPage*   m_settings_page   = nullptr;
    NavPage         m_current_page    = NavPage::Dashboard;

    // HUD overlay and system tray
    CompactOverlay  m_overlay;
    SystemTray      m_tray;
    HICON           m_icon_big   = nullptr;
    HICON           m_icon_small = nullptr;

    // SHM reader
    bool            m_shm_open   = false;
    uint64_t        m_data_ticks = 0;
    uint64_t        m_shm_read_ok = 0;
    uint64_t        m_shm_read_fail = 0;
    bool            m_logged_first_telemetry = false;
    bool            m_last_service_connected = false;
    std::string     m_service_display_address;

    // API key cache
    std::vector<ApiKeyInfo> m_key_cache;
    int64_t m_key_cache_stamp = 0;
    uint64_t m_api_ticks = 0;
    std::string m_active_log_session_id;

    struct TelemetrySource {
        std::string id;
        std::string label;
        std::string address;
        bool local = true;
        bool online = true;
    };
    std::string m_selected_source_id = "local";
    std::string m_selected_source_label = "This Device";
    std::unordered_map<uint32_t, float> m_remote_snapshot_values;

    // Window metrics
    UINT  m_width     = 1600;
    UINT  m_height    = 960;
    float m_dpi_scale = 1.0f;

    // ── Init / Teardown ───────────────────────────────────────────────────────
    bool InitD2DAndPages();
    void TeardownPages();
    void LoadConfig();
    void SaveConfig();
    void LoadProfile(const std::string& path);
    void SaveProfile(const std::string& path);
    void LoadMetricCatalog();
    void SaveMetricCatalog();
    void SyncPagesFromMetricCatalog();
    void SyncMetricCatalogFromPages();

    // ── Minimize / Restore logic ──────────────────────────────────────────────
    void DoMinimize();
    void DoRestoreFromOverlay();
    void DoRestoreFromTray();
    void EnsureTrayIcon();
    void HandleMenuCommand(UINT cmd);
    void ToggleMetricLoggingFromMenu();
    void SetHudPositionFromMenu(HudPosition pos);
    bool IsFleetMetricSelectionReady() const;
    bool IsFleetHostInstall() const;
    bool RecordingActive() const;
    std::vector<TelemetrySource> BuildTelemetrySources() const;
    std::vector<TrayDeviceOption> BuildTrayDeviceOptions(int& selected_remote_index) const;
    void SelectTelemetrySourceLocal();
    void SelectTelemetrySourceByIndex(size_t index);
    void ValidateSelectedTelemetrySource();
    bool FetchSelectedRemoteSnapshot();
    bool ReadRemoteSnapshotMetric(uint32_t id, float& out) const;
    void ShowDashboardSourceMenu();
    std::string ProductDisplayName() const;
    std::string ProductRoleName() const;
    std::wstring ProductWindowTitle() const;
    void RefreshServiceDisplayAddress();

    // ── Window messages ───────────────────────────────────────────────────────
    void OnSize(UINT w, UINT h);
    void OnRender();
    void OnDataTick();
    void OnApiTick();
    bool StartMetricLoggingSession(const std::string& label = "",
                                   const std::string& log_dir = "",
                                   const std::vector<uint32_t>& metric_ids = {});
    void StopMetricLoggingSession();
    bool OnMouseDown(float x, float y, int btn);
    void OnMouseMove(float x, float y);
    void OnMouseUp(float x, float y);
    void OnMouseWheel(float delta, float x, float y);
    void OnChar(wchar_t ch);
    void OnKeyDown(WPARAM vk);

    // ── Profile export/import ─────────────────────────────────────────────────
    void ExportProfileDialog();
    void ImportProfileDialog();

    static constexpr wchar_t CLASS_NAME[]  = L"TelemetryClientWnd";
    static constexpr int     RENDER_TIMER  = 1;  // 16 ms → ~60 fps
    static constexpr int     DATA_TIMER    = 2;  // 1000 ms → 1 Hz SHM read
    static constexpr int     API_TIMER     = 3;  // 5000 ms → key list refresh
};

} // namespace Client
