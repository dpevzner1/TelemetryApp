#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <commdlg.h>
#include <shlobj.h>
#include <windowsx.h>
#include <ws2tcpip.h>
#include <d2d1_1helper.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <utility>
#include <exception>
#include <algorithm>
#include <unordered_set>
#include <nlohmann/json.hpp>
#include <httplib.h>
#include <spdlog/spdlog.h>

#include "diagnostic_log.h"
#include "window.h"
#include "ipc/shm_reader.h"
#include "config/app_config.h"
#include "config/dashboard_profile.h"
#include "ui/compact_overlay.h"
#include "ui/gauge_icon.h"
#include "../shared/metric_ids.h"

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "dwmapi.lib")

using json = nlohmann::json;

namespace Client {

// ── Init ──────────────────────────────────────────────────────────────────────

struct ApiEndpoint {
    std::string host = "localhost";
    int port = 8765;
};

static ApiEndpoint ParseApiEndpoint(const AppConfig& cfg) {
    ApiEndpoint ep;
    ep.port = cfg.api_port > 0 ? cfg.api_port : 8765;
    std::string url = cfg.api_url.empty() ? "http://localhost:8765" : cfg.api_url;
    const std::string http = "http://";
    const std::string https = "https://";
    if (url.rfind(http, 0) == 0) url = url.substr(http.size());
    else if (url.rfind(https, 0) == 0) url = url.substr(https.size());
    size_t slash = url.find('/');
    if (slash != std::string::npos) url.resize(slash);
    size_t colon = url.rfind(':');
    if (colon != std::string::npos) {
        ep.host = url.substr(0, colon);
        int p = std::atoi(url.substr(colon + 1).c_str());
        if (p > 0) ep.port = p;
    } else if (!url.empty()) {
        ep.host = url;
    }
    return ep;
}

static httplib::Headers ApiHeaders(const AppConfig& cfg) {
    httplib::Headers h;
    if (!cfg.api_key.empty()) h.emplace("X-API-Key", cfg.api_key);
    return h;
}

static std::unique_ptr<httplib::Client> MakeApiClient(const AppConfig& cfg) {
    ApiEndpoint ep = ParseApiEndpoint(cfg);
    auto cli = std::make_unique<httplib::Client>(ep.host, ep.port);
    cli->set_connection_timeout(1, 0);
    cli->set_read_timeout(2, 0);
    cli->set_write_timeout(2, 0);
    return cli;
}

static bool IsPrivateLanIpv4(const std::string& ip) {
    return ip.rfind("10.", 0) == 0 ||
           ip.rfind("192.168.", 0) == 0 ||
           ip.rfind("172.16.", 0) == 0 ||
           ip.rfind("172.17.", 0) == 0 ||
           ip.rfind("172.18.", 0) == 0 ||
           ip.rfind("172.19.", 0) == 0 ||
           ip.rfind("172.20.", 0) == 0 ||
           ip.rfind("172.21.", 0) == 0 ||
           ip.rfind("172.22.", 0) == 0 ||
           ip.rfind("172.23.", 0) == 0 ||
           ip.rfind("172.24.", 0) == 0 ||
           ip.rfind("172.25.", 0) == 0 ||
           ip.rfind("172.26.", 0) == 0 ||
           ip.rfind("172.27.", 0) == 0 ||
           ip.rfind("172.28.", 0) == 0 ||
           ip.rfind("172.29.", 0) == 0 ||
           ip.rfind("172.30.", 0) == 0 ||
           ip.rfind("172.31.", 0) == 0;
}

static std::string ResolvePreferredLocalIpv4() {
    std::string first;
    WSADATA wsa{};
    bool wsa_started = WSAStartup(MAKEWORD(2, 2), &wsa) == 0;

    char host[256]{};
    if (gethostname(host, sizeof(host)) != 0 || host[0] == 0) {
        if (wsa_started) WSACleanup();
        return "127.0.0.1";
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_ADDRCONFIG;

    addrinfo* result = nullptr;
    if (getaddrinfo(host, nullptr, &hints, &result) != 0) {
        if (wsa_started) WSACleanup();
        return "127.0.0.1";
    }

    for (addrinfo* it = result; it; it = it->ai_next) {
        if (!it->ai_addr || it->ai_family != AF_INET) continue;
        char addr[INET_ADDRSTRLEN]{};
        auto* sin = reinterpret_cast<sockaddr_in*>(it->ai_addr);
        if (!inet_ntop(AF_INET, &sin->sin_addr, addr, sizeof(addr))) continue;

        std::string ip(addr);
        if (ip.rfind("127.", 0) == 0 || ip.rfind("169.254.", 0) == 0) continue;
        if (first.empty()) first = ip;
        if (IsPrivateLanIpv4(ip)) {
            freeaddrinfo(result);
            if (wsa_started) WSACleanup();
            return ip;
        }
    }
    freeaddrinfo(result);
    if (wsa_started) WSACleanup();
    return first.empty() ? "127.0.0.1" : first;
}

static ApiKeyInfo ApiKeyFromJson(const json& j) {
    ApiKeyInfo k;
    k.id = j.value("id", "");
    k.name = j.value("name", "");
    k.key_prefix = j.value("key_prefix", "");
    k.created_at_ms = j.value("created_at", static_cast<int64_t>(0));
    k.expiry_type = j.value("expiry_type", 0);
    k.expires_at_ms = j.value("expires_at", static_cast<int64_t>(0));
    k.active = j.value("active", false);
    k.status = j.value("status", k.active ? "Active" : "Revoked");
    return k;
}

static std::wstring Widen(const std::string& s) {
    return std::wstring(s.begin(), s.end());
}

static std::string FleetSourceId(const FleetDeviceRow& row) {
    if (!row.mac_hash.empty()) return "mac:" + row.mac_hash;
    if (!row.sensor_hash.empty()) return "sensor:" + row.sensor_hash;
    return "addr:" + row.address;
}

static bool SplitHostPortLocal(const std::string& address, std::string& host, int& port) {
    std::string s = address;
    if (s.rfind("http://", 0) == 0) s = s.substr(7);
    if (s.rfind("https://", 0) == 0) s = s.substr(8);
    size_t slash = s.find('/');
    if (slash != std::string::npos) s = s.substr(0, slash);
    size_t colon = s.rfind(':');
    if (colon == std::string::npos) {
        host = s;
        port = 8765;
        return !host.empty();
    }
    host = s.substr(0, colon);
    port = atoi(s.substr(colon + 1).c_str());
    if (port <= 0) port = 8765;
    return !host.empty();
}

static std::string Narrow(const std::wstring& s) {
    return std::string(s.begin(), s.end());
}

static bool BrowseForLogFolder(HWND owner, const std::string& initial, std::string& out_dir) {
    wchar_t display[MAX_PATH]{};
    BROWSEINFOW bi{};
    bi.hwndOwner = owner;
    bi.pszDisplayName = display;
    bi.lpszTitle = L"Choose where TelemetryApp should save metric logs";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return false;

    wchar_t path[MAX_PATH]{};
    bool ok = SHGetPathFromIDListW(pidl, path) == TRUE;
    CoTaskMemFree(pidl);
    if (!ok || path[0] == 0) return false;

    out_dir = Narrow(path);
    return true;
}

static std::vector<HudMetric> BuildHudMetricsFromIds(const std::vector<uint32_t>& ids,
                                                      const DashboardProfile& profile) {
    auto defaults = MakeDefaultHudMetrics();
    if (ids.empty()) return defaults;

    std::vector<HudMetric> out;
    for (uint32_t id : ids) {
        bool found = false;
        for (const auto& d : defaults) {
            if (d.metric_id == id) {
                out.push_back(d);
                found = true;
                break;
            }
        }
        if (found) continue;

        for (const auto& p : profile.panels) {
            if (p.metric_id != id) continue;
            float warn = p.unit == "%" ? 0.75f : 0.0f;
            float crit = p.unit == "%" ? 0.90f : 0.0f;
            out.push_back({id, p.label, p.unit, warn, crit, std::max(1.0f, p.y_max)});
            found = true;
            break;
        }
        if (!found) out.push_back({id, std::to_string(id), "", 0, 0, 100.0f});
    }
    return out;
}

bool AppWindow::Create(HINSTANCE hinstance) {
    DiagnosticLogInfo("AppWindow::Create begin.");
    m_hinstance = hinstance;

    DiagnosticLogInfo("Reading system DPI.");
    UINT dpi   = GetDpiForSystem();
    m_dpi_scale = dpi / 96.0f;
    DiagnosticLogInfo("System DPI read: " + std::to_string(dpi));

    DiagnosticLogInfo("Resolving client config from environment.");
    m_config.ResolveFromEnv();
    DiagnosticLogInfo("Loading client config.");
    LoadConfig();

    DiagnosticLogInfo("Registering window class.");
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hinstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    if (!RegisterClassExW(&wc)) {
        DiagnosticLogLastError("RegisterClassExW");
        return false;
    }

    DiagnosticLogInfo("Creating main window.");
    m_hwnd = CreateWindowExW(0, CLASS_NAME, ProductWindowTitle().c_str(),
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        (int)(m_width  * m_dpi_scale),
        (int)(m_height * m_dpi_scale),
        nullptr, nullptr, hinstance, this);
    if (!m_hwnd) {
        DiagnosticLogLastError("CreateWindowExW");
        return false;
    }
    DiagnosticLogInfo("Main window created.");

    // Gauge icon (programmatic — no .ico file needed)
    DiagnosticLogInfo("Setting gauge icon.");
    SetGaugeIcon(m_hwnd);

    BOOL dark = TRUE;
    DiagnosticLogInfo("Applying dark window attribute.");
    DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    DiagnosticLogInfo("Initializing D2D and pages.");
    if (!InitD2DAndPages()) {
        DiagnosticLogError("InitD2DAndPages failed.");
        return false;
    }

    DiagnosticLogInfo("Showing main window.");
    ShowWindow(m_hwnd, SW_SHOWDEFAULT);
    UpdateWindow(m_hwnd);

    // HUD compact overlay
    {
        m_overlay.SetMetrics(BuildHudMetricsFromIds(m_config.hud_metric_ids, m_profile));
        m_overlay.SetPosition((HudPosition)m_config.hud_position);
        m_overlay.SetFleetMenuVisible(IsFleetHostInstall());
        m_overlay.Create(hinstance, m_hwnd);
    }

    // System tray is always present; minimize behavior only controls what happens
    // to the main window when the user minimizes it.
    EnsureTrayIcon();

    if (m_config.start_minimized) {
        DiagnosticLogInfo("Start minimized enabled; applying configured minimize behavior.");
        DoMinimize();
    }

    m_shm_open = ShmReaderOpen();
    DiagnosticLogInfo(m_shm_open ? "Shared memory opened." : "Shared memory unavailable; service may not be running.");
    if (!m_shm_open) spdlog::warn("window: SHM unavailable — service may not be running");

    SetTimer(m_hwnd, RENDER_TIMER, 16,   nullptr);
    SetTimer(m_hwnd, DATA_TIMER,   1000, nullptr);
    SetTimer(m_hwnd, API_TIMER,    5000, nullptr);
    DiagnosticLogInfo("AppWindow::Create complete.");
    return true;
}

bool AppWindow::InitD2DAndPages() {
    DiagnosticLogInfo("InitD2DAndPages begin.");
    RECT r{};
    GetClientRect(m_hwnd, &r);
    m_width  = (UINT)r.right;
    m_height = (UINT)r.bottom;

    if (!m_d2d.Init(m_hwnd, m_width, m_height)) {
        spdlog::error("window: D2D init failed");
        DiagnosticLogError("D2D init failed.");
        return false;
    }

    m_sidebar        = new Sidebar(m_d2d);
    m_dashboard_page = new DashboardPage(m_d2d);
    m_api_page       = new ApiPage(m_d2d);
    m_metrics_page   = new MetricsPage(m_d2d);
    m_fleet_page     = new FleetPage(m_d2d);
    m_settings_page  = new SettingsPage(m_d2d);
    m_sidebar->SetFleetVisible(IsFleetHostInstall());
    m_sidebar->SetProductIdentity(ProductRoleName(), std::string("v") + APP_VERSION);

    m_sidebar->SetOnNav([this](NavPage p) {
        if (p == NavPage::Fleet && !IsFleetHostInstall()) p = NavPage::Dashboard;
        m_current_page = p;
        if (p == NavPage::Api) OnApiTick();
    });
    m_fleet_page->SetOnViewLocal([this]() {
        SelectTelemetrySourceLocal();
        m_current_page = NavPage::Dashboard;
        if (m_sidebar) m_sidebar->SetPage(NavPage::Dashboard);
    });
    m_fleet_page->SetOnViewRemote([this](const FleetDeviceRow& row) {
        std::string id = FleetSourceId(row);
        auto sources = BuildTelemetrySources();
        for (size_t i = 0; i < sources.size(); ++i) {
            if (sources[i].id == id) {
                SelectTelemetrySourceByIndex(i);
                return;
            }
        }
        MessageBoxW(m_hwnd,
            L"That device is not currently available as a trusted dashboard source. Refresh or enroll it first.",
            L"Fleet dashboard source unavailable",
            MB_ICONINFORMATION | MB_OK);
    });
    m_fleet_page->SetStoragePath(m_config.data_dir + "\\fleet_logging_jobs.json");
    m_fleet_page->SetOnChooseLogFolder([this](std::string& out_dir) -> bool {
        return BrowseForLogFolder(m_hwnd, m_config.log_dir, out_dir);
    });
    m_fleet_page->SetOnStartLocalJob([this](const FleetLoggingJob& job, std::string& error) -> bool {
        if (!m_active_log_session_id.empty()) StopMetricLoggingSession();
        m_config.log_dir = job.storage_dir;
        m_config.log_format = job.format;
        if (m_metrics_page) {
            m_metrics_page->SetLogDir(job.storage_dir);
            m_metrics_page->SetLogFormat(job.format);
        }
        SaveConfig();
        if (!StartMetricLoggingSession(job.label, job.storage_dir, m_metric_catalog.LoggedMetricIds())) {
            error = "The local session API rejected the fleet logging job. Check service status and folder write access.";
            return false;
        }
        if (m_metrics_page) m_metrics_page->SetLoggingEnabled(true);
        m_config.logging_enabled = true;
        SaveConfig();
        return true;
    });
    m_fleet_page->SetOnStopLocalJob([this]() {
        StopMetricLoggingSession();
        if (m_metrics_page) m_metrics_page->SetLoggingEnabled(false);
        m_config.logging_enabled = false;
        SaveConfig();
    });
    m_fleet_page->SetOnDevicesChanged([this]() {
        ValidateSelectedTelemetrySource();
        int selected = -1;
        auto options = BuildTrayDeviceOptions(selected);
        m_overlay.SetFleetDeviceOptions(options, selected);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    });

    std::string profile_path = m_config.dashboards_dir + "\\Default.json";
    if (!m_profile.Load(profile_path)) {
        m_profile = DashboardProfile::MakeDefault();
        m_profile.SetFilePath(profile_path);
    }
    LoadMetricCatalog();
    m_dashboard_page->SetProfile(&m_profile);
    m_dashboard_page->SetSourceLabel(m_selected_source_label);
    m_dashboard_page->SetOnSourceMenuRequested([this]() { ShowDashboardSourceMenu(); });
    SyncPagesFromMetricCatalog();
    m_metrics_page->SetLogDir(m_config.log_dir);
    m_metrics_page->SetLogFormat(m_config.log_format);
    m_metrics_page->SetLoggingEnabled(m_config.logging_enabled);
    m_metrics_page->SetOnLoggingEnableRequested([this](MetricsPage& page) -> bool {
        std::string selected;
        if (!BrowseForLogFolder(m_hwnd, m_config.log_dir, selected)) {
            DiagnosticLogInfo("Metric logging enable cancelled: no log folder selected.");
            return false;
        }
        m_config.log_dir = selected;
        page.SetLogDir(selected);
        CreateDirectoryA(selected.c_str(), nullptr);
        DiagnosticLogInfo("Metric logging folder selected: " + selected);
        SaveConfig();
        return true;
    });
    m_metrics_page->SetOnHudMetricsChanged([this](const std::vector<uint32_t>& ids) {
        m_config.hud_metric_ids = ids;
        m_metric_catalog.UpdateHudSelection(ids);
        m_overlay.SetMetrics(BuildHudMetricsFromIds(ids, m_profile));
        if (m_overlay.IsVisible()) m_overlay.Render();
        DiagnosticLogInfo("HUD metric selection updated: count=" + std::to_string(ids.size()) + ".");
        SaveMetricCatalog();
        SaveConfig();
    });

    m_settings_page->SetConfig(&m_config);
    m_settings_page->SetOnSave([this]() {
        // Propagate minimize behavior / HUD position immediately
        m_overlay.SetPosition((HudPosition)m_config.hud_position);
        SaveConfig();
    });

    m_api_page->SetCallbacks(
        [this](const std::string& name, int expiry_type, int64_t custom_ms) -> std::string {
            try {
                auto cli = MakeApiClient(m_config);
                json body;
                body["name"] = name.empty() ? "Desktop key" : name;
                body["expiry_type"] = expiry_type;
                body["expires_at"] = custom_ms;
                auto res = cli->Post("/api/v1/keys", ApiHeaders(m_config),
                                     body.dump(), "application/json");
                if (!res || (res->status != 200 && res->status != 201)) return "";
                json resp = json::parse(res->body);
                std::string key = resp.value("key", "");
                if (!key.empty() && m_config.api_key.empty()) {
                    m_config.api_key = key;
                    SaveConfig();
                }
                OnApiTick();
                return key;
            } catch (...) {
                return "";
            }
        },
        [this](const std::string& id) {
            std::string deleted_prefix;
            for (const auto& k : m_key_cache) {
                if (k.id == id) {
                    deleted_prefix = k.key_prefix;
                    break;
                }
            }
            auto cli = MakeApiClient(m_config);
            auto res = cli->Delete(("/api/v1/keys/" + id).c_str(), ApiHeaders(m_config));
            if (res && (res->status == 204 || res->status == 200)) {
                if (!deleted_prefix.empty() &&
                    m_config.api_key.rfind(deleted_prefix, 0) == 0) {
                    m_config.api_key.clear();
                    SaveConfig();
                    DiagnosticLogInfo("Deleted active API key; cleared saved desktop API key.");
                }
                OnApiTick();
            }
        },
        [this](const std::string& id) -> std::string {
            try {
                auto cli = MakeApiClient(m_config);
                auto res = cli->Post(("/api/v1/keys/" + id + "/rotate").c_str(),
                                     ApiHeaders(m_config), "", "application/json");
                if (!res || res->status != 200) return "";
                json resp = json::parse(res->body);
                std::string key = resp.value("key", "");
                if (!key.empty()) {
                    m_config.api_key = key;
                    SaveConfig();
                }
                OnApiTick();
                return key;
            } catch (...) {
                return "";
            }
        }
    );

    DiagnosticLogInfo("InitD2DAndPages complete.");
    return true;
}

void AppWindow::TeardownPages() {
    delete m_settings_page;   m_settings_page  = nullptr;
    delete m_fleet_page;      m_fleet_page     = nullptr;
    delete m_metrics_page;    m_metrics_page   = nullptr;
    delete m_api_page;        m_api_page       = nullptr;
    delete m_dashboard_page;  m_dashboard_page = nullptr;
    delete m_sidebar;         m_sidebar        = nullptr;
}

void AppWindow::Destroy() {
    DiagnosticLogInfo("AppWindow::Destroy begin.");
    KillTimer(m_hwnd, RENDER_TIMER);
    KillTimer(m_hwnd, DATA_TIMER);
    KillTimer(m_hwnd, API_TIMER);
    StopMetricLoggingSession();
    m_overlay.Destroy();
    m_tray.Remove();
    if (m_shm_open) { ShmReaderClose(); m_shm_open = false; }
    if (m_dashboard_page) {
        m_dashboard_page->FlushToProfile();
        std::string pp = m_profile.FilePath();
        if (!pp.empty()) m_profile.Save(pp);
    }
    SaveConfig();
    TeardownPages();
    m_d2d.Destroy();
    DiagnosticLogInfo("AppWindow::Destroy complete.");
}

// ── Config ────────────────────────────────────────────────────────────────────

void AppWindow::LoadConfig() {
    std::string path = m_config.data_dir + "\\client.json";
    m_config.Load(path);
}

void AppWindow::SaveConfig() {
    std::string path = m_config.data_dir + "\\client.json";
    CreateDirectoryA(m_config.data_dir.c_str(), nullptr);
    m_config.Save(path);
}

void AppWindow::LoadMetricCatalog() {
    std::vector<uint32_t> hud_ids = m_config.hud_metric_ids;
    if (hud_ids.empty()) {
        for (const auto& hm : MakeDefaultHudMetrics()) hud_ids.push_back(hm.metric_id);
    }

    std::string path = m_config.data_dir + "\\metric_catalog.json";
    bool loaded = m_metric_catalog.Load(path);
    m_metric_catalog.BuildFromDashboard(m_profile, hud_ids);
    m_metric_catalog.ApplyToDashboard(m_profile);
    m_config.hud_metric_ids = m_metric_catalog.HudMetricIds();
    DiagnosticLogInfo(std::string("Metric catalog ") + (loaded ? "loaded" : "initialized") +
                      ": entries=" + std::to_string(m_metric_catalog.Entries().size()) + ".");
    SaveMetricCatalog();
}

void AppWindow::SaveMetricCatalog() {
    CreateDirectoryA(m_config.data_dir.c_str(), nullptr);
    std::string path = m_config.data_dir + "\\metric_catalog.json";
    if (!m_metric_catalog.Save(path)) {
        DiagnosticLogWarn("Metric catalog save failed: " + path);
    }
}

void AppWindow::SyncPagesFromMetricCatalog() {
    m_metric_catalog.ApplyToDashboard(m_profile);
    m_metrics_page->SyncFromProfile(m_profile.panels);
    m_metrics_page->SetHudMetricIds(m_metric_catalog.HudMetricIds());
    m_config.hud_metric_ids = m_metric_catalog.HudMetricIds();
    m_overlay.SetMetrics(BuildHudMetricsFromIds(m_config.hud_metric_ids, m_profile));
}

void AppWindow::SyncMetricCatalogFromPages() {
    if (!m_metrics_page) return;
    m_metric_catalog.UpdateLoggingSelection(m_metrics_page->GetLoggedMetricIds());
    m_metric_catalog.UpdateHudSelection(m_metrics_page->GetHudMetricIds());
    m_metric_catalog.ApplyToDashboard(m_profile);
    m_config.hud_metric_ids = m_metric_catalog.HudMetricIds();
}

void AppWindow::LoadProfile(const std::string& path) {
    if (m_profile.Load(path)) {
        m_dashboard_page->SetProfile(&m_profile);
        LoadMetricCatalog();
        SyncPagesFromMetricCatalog();
    }
}

void AppWindow::SaveProfile(const std::string& path) {
    m_dashboard_page->FlushToProfile();
    SyncMetricCatalogFromPages();
    m_profile.Save(path);
    SaveMetricCatalog();
}

// ── Message loop ──────────────────────────────────────────────────────────────

void AppWindow::RunMessageLoop() {
    DiagnosticLogInfo("RunMessageLoop begin.");
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (msg.message == WM_QUIT) break;
    }
    DiagnosticLogInfo("RunMessageLoop end.");
}

// ── Minimize / Restore ────────────────────────────────────────────────────────

void AppWindow::EnsureTrayIcon() {
    if (m_tray.IsAdded()) return;
    HICON tray_icon = (HICON)LoadImageW(GetModuleHandleW(nullptr), L"IDI_APP_ICON",
        IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR);
    if (!tray_icon) tray_icon = CreateGaugeIcon(32);
    if (!tray_icon) tray_icon = (HICON)SendMessageW(m_hwnd, WM_GETICON, ICON_BIG, 0);
    std::wstring tip = ProductWindowTitle();
    bool added = m_tray.Add(m_hwnd, tray_icon, tip.c_str());
    DiagnosticLogInfo(std::string("System tray icon ") + (added ? "added." : "add failed."));
}

void AppWindow::DoMinimize() {
    EnsureTrayIcon();
    switch (m_config.minimize_behavior) {
    case MinimizeBehavior::HUDMode:
        ShowWindow(m_hwnd, SW_HIDE);
        m_overlay.Show();
        break;
    case MinimizeBehavior::SystemTray:
        ShowWindow(m_hwnd, SW_HIDE);
        // Tray icon already present — balloon tip optional
        break;
    case MinimizeBehavior::Normal:
    default:
        ShowWindow(m_hwnd, SW_MINIMIZE);
        break;
    }
}

void AppWindow::DoRestoreFromOverlay() {
    m_overlay.Hide();
    ShowWindow(m_hwnd, SW_RESTORE);
    SetForegroundWindow(m_hwnd);
}

void AppWindow::DoRestoreFromTray() {
    ShowWindow(m_hwnd, SW_RESTORE);
    SetForegroundWindow(m_hwnd);
}

// ── Render ────────────────────────────────────────────────────────────────────

void AppWindow::OnRender() {
    if (!m_d2d.DC()) return;
    if (!m_sidebar || !m_dashboard_page || !m_api_page || !m_metrics_page || !m_fleet_page || !m_settings_page) return;
    if (m_current_page == NavPage::Fleet && !IsFleetHostInstall()) {
        m_current_page = NavPage::Dashboard;
        m_sidebar->SetPage(NavPage::Dashboard);
    }
    m_d2d.BeginDraw();
    m_d2d.Clear({0.09f, 0.09f, 0.11f, 1.0f});

    float sw = Sidebar::WIDTH;
    float cw = (float)m_width - sw;
    float h  = (float)m_height;

    m_sidebar->service_connected = m_shm_open;
    m_sidebar->active_key_count  = (int)m_key_cache.size();
    m_sidebar->service_address   = m_last_service_connected ? m_service_display_address : "LAN: unavailable";
    ValidateSelectedTelemetrySource();
    {
        int selected_remote = -1;
        auto options = BuildTrayDeviceOptions(selected_remote);
        m_overlay.SetFleetDeviceOptions(options, selected_remote);
    }
    m_sidebar->Draw(h, m_dpi_scale);

    switch (m_current_page) {
    case NavPage::Dashboard:
        m_dashboard_page->Draw(sw, 0, cw, h, m_dpi_scale);
        break;
    case NavPage::Api:
        m_api_page->Draw(sw, 0, cw, h, m_dpi_scale);
        break;
    case NavPage::Metrics:
        m_metrics_page->Draw(sw, 0, cw, h, m_dpi_scale);
        break;
    case NavPage::Fleet:
        m_fleet_page->SetServiceConnected(m_shm_open);
        m_fleet_page->Draw(sw, 0, cw, h, m_dpi_scale);
        break;
    case NavPage::Settings:
        m_settings_page->service_connected = m_shm_open;
        m_settings_page->Draw(sw, 0, cw, h, m_dpi_scale);
        break;
    default: break;
    }

    HRESULT hr = m_d2d.EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        m_d2d.Destroy();
        TeardownPages();
        InitD2DAndPages();
    }
    m_d2d.Present();
}

// ── Data tick (1 Hz) ──────────────────────────────────────────────────────────

void AppWindow::OnDataTick() {
    ++m_data_ticks;
    if (!m_shm_open) {
        m_shm_open = ShmReaderOpen();
        if (m_shm_open) {
            DiagnosticLogInfo("Shared memory reconnected on data tick.");
        } else {
            if (m_last_service_connected || m_data_ticks <= 5 || (m_data_ticks % 30) == 0) {
                DiagnosticLogWarn("Telemetry read skipped: shared memory unavailable on data tick " +
                    std::to_string(m_data_ticks) + ".");
            }
            m_last_service_connected = false;
            return;
        }
    }

    uint32_t tick_ok = 0;
    uint32_t tick_fail = 0;
    auto read_metric = [&](uint32_t id, double& out) -> bool {
        bool ok = ShmReadMetric(id, out);
        if (ok) { ++tick_ok; ++m_shm_read_ok; }
        else { ++tick_fail; ++m_shm_read_fail; }
        return ok;
    };

    bool remote_source = IsFleetHostInstall() && m_selected_source_id != "local";
    if (remote_source) FetchSelectedRemoteSnapshot();

    // Feed HUD overlay
    if (m_overlay.IsVisible()) {
        for (const auto& hm : m_overlay.Metrics()) {
            if (remote_source) {
                float v = 0.0f;
                if (ReadRemoteSnapshotMetric(hm.metric_id, v)) m_overlay.UpdateValue(hm.metric_id, v);
            } else {
                double v = 0;
                read_metric(hm.metric_id, v);
                m_overlay.UpdateValue(hm.metric_id, (float)v);
            }
        }
    }

    double poll_ms = 0;
    read_metric(388, poll_ms);
    m_settings_page->poll_duration_ms = poll_ms;
    m_settings_page->active_keys      = (int)m_key_cache.size();

    double cpu = 0.0, mem = 0.0;
    read_metric(MetricId::CPU_USAGE_TOTAL, cpu);
    read_metric(MetricId::MEM_PERCENT, mem);

    if (!m_logged_first_telemetry && tick_ok > 0) {
        DiagnosticLogInfo("First successful telemetry read: cpu_pct=" + std::to_string(cpu) +
            ", mem_pct=" + std::to_string(mem) +
            ", poll_ms=" + std::to_string(poll_ms) + ".");
        m_logged_first_telemetry = true;
    }

    if (!m_last_service_connected && tick_ok > 0) {
        DiagnosticLogInfo("Telemetry read status changed: online.");
        RefreshServiceDisplayAddress();
    } else if (m_last_service_connected && tick_ok == 0) {
        DiagnosticLogWarn("Telemetry read status changed: no successful reads.");
    }
    m_last_service_connected = tick_ok > 0;
    if (m_last_service_connected && m_service_display_address.empty()) {
        RefreshServiceDisplayAddress();
    }

    if (m_data_ticks <= 10 || (m_data_ticks % 60) == 0 || tick_fail > 0) {
        DiagnosticLogInfo("Telemetry read tick " + std::to_string(m_data_ticks) +
            ": ok=" + std::to_string(tick_ok) +
            ", fail=" + std::to_string(tick_fail) +
            ", total_ok=" + std::to_string(m_shm_read_ok) +
            ", total_fail=" + std::to_string(m_shm_read_fail) + ".");
    }

    if (m_current_page != NavPage::Dashboard) return;
    auto* prof = m_dashboard_page->Profile();
    if (!prof) return;

    auto readf = [&](uint32_t id) -> float {
        if (remote_source) {
            float rv = 0.0f;
            return ReadRemoteSnapshotMetric(id, rv) ? rv : 0.0f;
        }
        double v = 0.0;
        read_metric(id, v);
        return (float)v;
    };

    for (const auto& mp : prof->panels) {
        if (!mp.visible) continue;
        m_dashboard_page->PushMetricValue(mp.metric_id, readf(mp.metric_id));

        if (mp.viz_type == VizType::DualLine)
            m_dashboard_page->PushMetricValue(mp.metric_id + 1, readf(mp.metric_id + 1));

        if (mp.viz_type == VizType::HeatMap)
            for (int c = 1; c < 32; ++c)
                m_dashboard_page->PushMetricValue(mp.metric_id + c, readf(mp.metric_id + c));
    }
}

// ── API tick (5 s) ────────────────────────────────────────────────────────────

void AppWindow::OnApiTick() {
    ++m_api_ticks;
    try {
        auto cli = MakeApiClient(m_config);
        auto res = cli->Get("/api/v1/keys", ApiHeaders(m_config));
        if (res && res->status == 200) {
            json arr = json::parse(res->body);
            std::vector<ApiKeyInfo> keys;
            if (arr.is_array()) {
                for (const auto& item : arr) keys.push_back(ApiKeyFromJson(item));
            }
            m_key_cache = std::move(keys);
            if (m_api_ticks <= 5 || (m_api_ticks % 12) == 0) {
                DiagnosticLogInfo("API key list refresh succeeded: keys=" +
                    std::to_string(m_key_cache.size()) + ".");
            }
        } else {
            DiagnosticLogWarn("API key list refresh failed: status=" +
                std::to_string(res ? res->status : 0) + ".");
        }
    } catch (const std::exception& ex) {
        DiagnosticLogWarn(std::string("API key list refresh exception: ") + ex.what());
    } catch (...) {
        DiagnosticLogWarn("API key list refresh exception: unknown.");
    }
    m_api_page->SetKeys(m_key_cache);
}

// ── Mouse / keyboard ──────────────────────────────────────────────────────────

bool AppWindow::StartMetricLoggingSession(const std::string& label,
                                          const std::string& log_dir,
                                          const std::vector<uint32_t>& metric_ids) {
    try {
        auto cli = MakeApiClient(m_config);

        if (m_config.api_key.empty()) {
            json key_body;
            key_body["name"] = "Desktop logging key";
            key_body["expiry_type"] = 0;
            key_body["expires_at"] = 0;
            auto key_res = cli->Post("/api/v1/keys", key_body.dump(), "application/json");
            if (key_res && (key_res->status == 200 || key_res->status == 201)) {
                json key_json = json::parse(key_res->body);
                m_config.api_key = key_json.value("key", "");
                SaveConfig();
                DiagnosticLogInfo("Created local API key for metric logging session.");
            }
        }

        json body;
        body["label"] = label.empty() ? "TelemetryApp desktop logging" : label;
        body["log_dir"] = log_dir.empty() ? m_config.log_dir : log_dir;
        body["duration_sec"] = 0;
        body["metric_ids"] = metric_ids.empty() ? m_metric_catalog.LoggedMetricIds() : metric_ids;

        auto res = cli->Post("/api/v1/sessions", ApiHeaders(m_config),
                             body.dump(), "application/json");
        if (!res || res->status != 201) {
            DiagnosticLogWarn("Metric logging session start failed: status=" +
                std::to_string(res ? res->status : 0) + ".");
            return false;
        }
        json resp = json::parse(res->body);
        m_active_log_session_id = resp.value("session_id", "");
        std::string log_path = resp.value("log_path", "");
        DiagnosticLogInfo("Metric logging session started: id=" + m_active_log_session_id +
                          ", path=" + log_path + ".");
        return !m_active_log_session_id.empty();
    } catch (const std::exception& ex) {
        DiagnosticLogWarn(std::string("Metric logging session start exception: ") + ex.what());
        return false;
    } catch (...) {
        DiagnosticLogWarn("Metric logging session start exception: unknown.");
        return false;
    }
}

void AppWindow::StopMetricLoggingSession() {
    if (m_active_log_session_id.empty()) return;
    try {
        auto cli = MakeApiClient(m_config);
        std::string path = "/api/v1/sessions/" + m_active_log_session_id + "/stop";
        auto res = cli->Post(path.c_str(), ApiHeaders(m_config), "", "application/json");
        DiagnosticLogInfo("Metric logging session stop requested: id=" + m_active_log_session_id +
                          ", status=" + std::to_string(res ? res->status : 0) + ".");
    } catch (const std::exception& ex) {
        DiagnosticLogWarn(std::string("Metric logging session stop exception: ") + ex.what());
    } catch (...) {
        DiagnosticLogWarn("Metric logging session stop exception: unknown.");
    }
    m_active_log_session_id.clear();
}

bool AppWindow::IsFleetMetricSelectionReady() const {
    return IsFleetHostInstall() && m_fleet_page != nullptr && m_last_service_connected;
}

bool AppWindow::IsFleetHostInstall() const {
    return _stricmp(m_config.install_mode.c_str(), "FleetHost") == 0;
}

std::vector<AppWindow::TelemetrySource> AppWindow::BuildTelemetrySources() const {
    std::vector<TelemetrySource> out;
    if (!IsFleetHostInstall() || !m_fleet_page) return out;
    for (const auto& row : m_fleet_page->Devices()) {
        if (row.local || !row.trusted) continue;
        TelemetrySource src;
        src.id = FleetSourceId(row);
        src.label = row.name.empty() ? row.address : row.name;
        src.address = row.address;
        src.local = false;
        src.online = row.state == "Online";
        out.push_back(src);
    }
    return out;
}

std::vector<TrayDeviceOption> AppWindow::BuildTrayDeviceOptions(int& selected_remote_index) const {
    selected_remote_index = -1;
    std::vector<TrayDeviceOption> options;
    auto sources = BuildTelemetrySources();
    for (size_t i = 0; i < sources.size() && i < 200; ++i) {
        TrayDeviceOption opt;
        std::string label = sources[i].label + " (" + sources[i].address + ")";
        opt.label = Widen(label);
        opt.online = sources[i].online;
        opt.selected = sources[i].id == m_selected_source_id;
        if (opt.selected) selected_remote_index = (int)i;
        options.push_back(std::move(opt));
    }
    return options;
}

void AppWindow::SelectTelemetrySourceLocal() {
    m_selected_source_id = "local";
    m_selected_source_label = "This Device";
    m_remote_snapshot_values.clear();
    if (m_dashboard_page) {
        m_dashboard_page->SetSourceLabel(m_selected_source_label);
        m_dashboard_page->ClearMetricValues();
    }
    DiagnosticLogInfo("Dashboard telemetry source selected: This Device.");
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void AppWindow::SelectTelemetrySourceByIndex(size_t index) {
    auto sources = BuildTelemetrySources();
    if (index >= sources.size()) {
        SelectTelemetrySourceLocal();
        return;
    }
    const auto& src = sources[index];
    if (!src.online) {
        MessageBoxW(m_hwnd,
            L"That fleet device is currently offline. Refresh or re-enroll it before using it as a live dashboard source.",
            L"Fleet device offline",
            MB_ICONINFORMATION | MB_OK);
        return;
    }
    m_selected_source_id = src.id;
    m_selected_source_label = src.label;
    m_remote_snapshot_values.clear();
    if (m_dashboard_page) {
        m_dashboard_page->SetSourceLabel("Fleet Device: " + src.label);
        m_dashboard_page->ClearMetricValues();
    }
    m_current_page = NavPage::Dashboard;
    if (m_sidebar) m_sidebar->SetPage(NavPage::Dashboard);
    ShowWindow(m_hwnd, SW_RESTORE);
    SetForegroundWindow(m_hwnd);
    DiagnosticLogInfo("Dashboard telemetry source selected: " + src.label + " at " + src.address + ".");
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void AppWindow::ValidateSelectedTelemetrySource() {
    if (m_selected_source_id == "local") return;
    auto sources = BuildTelemetrySources();
    for (const auto& src : sources) {
        if (src.id == m_selected_source_id) return;
    }
    DiagnosticLogWarn("Selected fleet telemetry source was removed or is no longer trusted; returning to local dashboard.");
    SelectTelemetrySourceLocal();
}

static void SnapshotPut(std::unordered_map<uint32_t, float>& values, uint32_t id, const json& obj, const char* key) {
    if (obj.is_object() && obj.contains(key) && obj[key].is_number()) {
        values[id] = obj[key].get<float>();
    }
}

bool AppWindow::FetchSelectedRemoteSnapshot() {
    if (m_selected_source_id == "local") return false;
    auto sources = BuildTelemetrySources();
    auto it = std::find_if(sources.begin(), sources.end(), [&](const TelemetrySource& s) {
        return s.id == m_selected_source_id;
    });
    if (it == sources.end()) {
        SelectTelemetrySourceLocal();
        return false;
    }

    std::string host;
    int port = 8765;
    if (!SplitHostPortLocal(it->address, host, port)) return false;
    try {
        httplib::Client cli(host, port);
        cli.set_connection_timeout(1, 0);
        cli.set_read_timeout(2, 0);
        auto res = cli.Get("/api/v1/lab/snapshot");
        if (!res || res->status != 200) {
            DiagnosticLogWarn("Remote dashboard snapshot failed for " + it->address +
                              ": status=" + std::to_string(res ? res->status : 0) + ".");
            return false;
        }
        json j = json::parse(res->body, nullptr, false);
        if (!j.is_object()) return false;

        std::unordered_map<uint32_t, float> values;
        const json cpu = j.value("cpu", json::object());
        SnapshotPut(values, MetricId::CPU_USAGE_TOTAL, cpu, "usage_total_pct");
        SnapshotPut(values, MetricId::CPU_FREQ_ACTUAL_MHZ, cpu, "freq_actual_mhz");
        if (cpu.contains("per_core_pct") && cpu["per_core_pct"].is_array()) {
            for (size_t i = 0; i < cpu["per_core_pct"].size() && i < 32; ++i) {
                if (cpu["per_core_pct"][i].is_number()) values[cpu_core_metric((uint32_t)i)] = cpu["per_core_pct"][i].get<float>();
            }
        }

        const json mem = j.value("memory", json::object());
        SnapshotPut(values, MetricId::MEM_TOTAL_GB, mem, "total_gb");
        SnapshotPut(values, MetricId::MEM_USED_GB, mem, "used_gb");
        SnapshotPut(values, MetricId::MEM_AVAILABLE_GB, mem, "available_gb");
        SnapshotPut(values, MetricId::MEM_PERCENT, mem, "percent");
        SnapshotPut(values, MetricId::MEM_SWAP_USED_GB, mem, "swap_used_gb");
        SnapshotPut(values, MetricId::MEM_SWAP_PERCENT, mem, "swap_pct");
        SnapshotPut(values, MetricId::MEM_STANDBY_GB, mem, "standby_gb");
        SnapshotPut(values, MetricId::MEM_PAGE_FAULT_RATE, mem, "page_fault_rate");

        if (j.contains("gpus") && j["gpus"].is_array()) {
            for (size_t gi = 0; gi < j["gpus"].size() && gi < MetricId::GPU_MAX_COUNT; ++gi) {
                const json& g = j["gpus"][gi];
                SnapshotPut(values, gpu_metric((uint32_t)gi, GpuOff::USAGE_PCT), g, "usage_pct");
                SnapshotPut(values, gpu_metric((uint32_t)gi, GpuOff::VRAM_USED_MB), g, "vram_used_mb");
                SnapshotPut(values, gpu_metric((uint32_t)gi, GpuOff::VRAM_TOTAL_MB), g, "vram_total_mb");
                SnapshotPut(values, gpu_metric((uint32_t)gi, GpuOff::VRAM_PCT), g, "vram_pct");
                SnapshotPut(values, gpu_metric((uint32_t)gi, GpuOff::TEMP_C), g, "temp_c");
                SnapshotPut(values, gpu_metric((uint32_t)gi, GpuOff::POWER_W), g, "power_w");
                SnapshotPut(values, gpu_metric((uint32_t)gi, GpuOff::FAN_PCT), g, "fan_pct");
                SnapshotPut(values, gpu_metric((uint32_t)gi, GpuOff::CLOCK_CORE_MHZ), g, "clock_core_mhz");
                SnapshotPut(values, gpu_metric((uint32_t)gi, GpuOff::CLOCK_MEM_MHZ), g, "clock_mem_mhz");
                SnapshotPut(values, gpu_metric((uint32_t)gi, GpuOff::PDH_UTIL_PCT), g, "pdh_util_pct");
                SnapshotPut(values, gpu_metric((uint32_t)gi, GpuOff::ENCODER_PCT), g, "encoder_pct");
                SnapshotPut(values, gpu_metric((uint32_t)gi, GpuOff::DECODER_PCT), g, "decoder_pct");
                SnapshotPut(values, gpu_metric((uint32_t)gi, GpuOff::SM_UTIL_PCT), g, "sm_util_pct");
                SnapshotPut(values, gpu_metric((uint32_t)gi, GpuOff::MEM_BW_UTIL_PCT), g, "mem_bw_util_pct");
                SnapshotPut(values, gpu_metric((uint32_t)gi, GpuOff::CUDA_CC_MAJOR), g, "cuda_cc_major");
                SnapshotPut(values, gpu_metric((uint32_t)gi, GpuOff::CUDA_CC_MINOR), g, "cuda_cc_minor");
                SnapshotPut(values, gpu_metric((uint32_t)gi, GpuOff::TENSOR_CORE_GEN), g, "tensor_core_gen");
                if (g.contains("has_tensor_cores")) {
                    values[gpu_metric((uint32_t)gi, GpuOff::TENSOR_ACTIVE)] = g.value("has_tensor_cores", false) ? 1.0f : 0.0f;
                }
            }
        }

        if (j.contains("disk") && j["disk"].is_array()) {
            for (size_t di = 0; di < j["disk"].size() && di < MetricId::DISK_MAX_COUNT; ++di) {
                const json& d = j["disk"][di];
                SnapshotPut(values, disk_metric((uint32_t)di, DiskOff::READ_BYTES_S), d, "read_bytes_s");
                SnapshotPut(values, disk_metric((uint32_t)di, DiskOff::WRITE_BYTES_S), d, "write_bytes_s");
                SnapshotPut(values, disk_metric((uint32_t)di, DiskOff::READ_IOPS), d, "read_iops");
                SnapshotPut(values, disk_metric((uint32_t)di, DiskOff::WRITE_IOPS), d, "write_iops");
                SnapshotPut(values, disk_metric((uint32_t)di, DiskOff::BUSY_PCT), d, "busy_pct");
            }
        }

        if (j.contains("network") && j["network"].is_array()) {
            for (size_t ni = 0; ni < j["network"].size() && ni < MetricId::NET_MAX_COUNT; ++ni) {
                const json& n = j["network"][ni];
                SnapshotPut(values, net_metric((uint32_t)ni, NetOff::RECV_BYTES_S), n, "recv_bytes_s");
                SnapshotPut(values, net_metric((uint32_t)ni, NetOff::SENT_BYTES_S), n, "sent_bytes_s");
                SnapshotPut(values, net_metric((uint32_t)ni, NetOff::RECV_PKTS_S), n, "recv_pkts_s");
                SnapshotPut(values, net_metric((uint32_t)ni, NetOff::SENT_PKTS_S), n, "sent_pkts_s");
            }
        }

        const json self = j.value("self", json::object());
        SnapshotPut(values, MetricId::SELF_CPU_PCT, self, "cpu_pct");

        if (j.contains("watched") && j["watched"].is_array()) {
            for (size_t wi = 0; wi < j["watched"].size() && wi < MetricId::WATCH_MAX_COUNT; ++wi) {
                const json& w = j["watched"][wi];
                SnapshotPut(values, watch_metric((uint32_t)wi, WatchOff::CPU_PCT), w, "cpu_pct");
                SnapshotPut(values, watch_metric((uint32_t)wi, WatchOff::PRIVATE_MB), w, "mem_private_mb");
                SnapshotPut(values, watch_metric((uint32_t)wi, WatchOff::GPU_SM_PCT), w, "gpu_sm_pct");
                SnapshotPut(values, watch_metric((uint32_t)wi, WatchOff::GPU_VRAM_MB), w, "gpu_vram_mb");
                SnapshotPut(values, watch_metric((uint32_t)wi, WatchOff::DISK_READ_BYTES_S), w, "disk_read_bytes_s");
                SnapshotPut(values, watch_metric((uint32_t)wi, WatchOff::DISK_WRITE_BYTES_S), w, "disk_write_bytes_s");
                SnapshotPut(values, watch_metric((uint32_t)wi, WatchOff::PAGE_FAULTS_S), w, "page_faults_s");
                SnapshotPut(values, watch_metric((uint32_t)wi, WatchOff::UPTIME_S), w, "uptime_sec");
                SnapshotPut(values, watch_metric((uint32_t)wi, WatchOff::THREADS), w, "threads");
                SnapshotPut(values, watch_metric((uint32_t)wi, WatchOff::HANDLES), w, "handles");
            }
        }

        m_remote_snapshot_values = std::move(values);
        return true;
    } catch (const std::exception& ex) {
        DiagnosticLogWarn(std::string("Remote dashboard snapshot exception: ") + ex.what());
        return false;
    } catch (...) {
        DiagnosticLogWarn("Remote dashboard snapshot exception: unknown.");
        return false;
    }
}

bool AppWindow::ReadRemoteSnapshotMetric(uint32_t id, float& out) const {
    auto it = m_remote_snapshot_values.find(id);
    if (it == m_remote_snapshot_values.end()) return false;
    out = it->second;
    return true;
}

void AppWindow::ShowDashboardSourceMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (m_selected_source_id == "local" ? MF_CHECKED : 0),
                TRAY_CMD_DASHBOARD_THIS_DEVICE, L"This Device");

    if (IsFleetHostInstall()) {
        auto sources = BuildTelemetrySources();
        if (!sources.empty()) AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        for (size_t i = 0; i < sources.size() && i < 200; ++i) {
            std::string label = sources[i].label + " (" + sources[i].address + ")";
            UINT flags = sources[i].online ? MF_STRING : MF_GRAYED;
            if (sources[i].id == m_selected_source_id) flags |= MF_CHECKED;
            AppendMenuW(menu, flags, TRAY_CMD_DASHBOARD_DEVICE_BASE + (UINT)i,
                        Widen(label).c_str());
        }
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, TRAY_CMD_MANAGE_FLEET, L"Manage Fleet...");
    }

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(m_hwnd);
    UINT cmd = (UINT)TrackPopupMenu(menu,
        TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_TOPALIGN,
        pt.x, pt.y, 0, m_hwnd, nullptr);
    PostMessageW(m_hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (cmd) HandleMenuCommand(cmd);
}

std::string AppWindow::ProductDisplayName() const {
    if (_stricmp(m_config.install_mode.c_str(), "FleetHost") == 0) return "TelemetryApp Fleet Manager";
    if (_stricmp(m_config.install_mode.c_str(), "SensorClient") == 0) return "TelemetryApp Sensor";
    return "TelemetryApp Local Monitor";
}

std::string AppWindow::ProductRoleName() const {
    if (_stricmp(m_config.install_mode.c_str(), "FleetHost") == 0) return "Fleet Manager";
    if (_stricmp(m_config.install_mode.c_str(), "SensorClient") == 0) return "Sensor";
    return "Local Monitor";
}

std::wstring AppWindow::ProductWindowTitle() const {
    std::string title = ProductDisplayName() + " v" + APP_VERSION;
    return std::wstring(title.begin(), title.end());
}

void AppWindow::RefreshServiceDisplayAddress() {
    int port = m_config.api_port > 0 ? m_config.api_port : 8765;
    std::string ip = ResolvePreferredLocalIpv4();
    m_service_display_address = "LAN: " + ip + ":" + std::to_string(port);
    DiagnosticLogInfo("Service display address resolved: " + m_service_display_address + ".");
}

void AppWindow::SetHudPositionFromMenu(HudPosition pos) {
    m_config.hud_position = static_cast<HudPositionCfg>(static_cast<int>(pos));
    m_overlay.SetPosition(pos);
    SaveConfig();
    DiagnosticLogInfo("HUD orientation changed from context menu.");
}

void AppWindow::ToggleMetricLoggingFromMenu() {
    bool was_logging = m_metrics_page && m_metrics_page->LoggingEnabled();
    if (was_logging) {
        StopMetricLoggingSession();
        m_config.logging_enabled = false;
        if (m_metrics_page) m_metrics_page->SetLoggingEnabled(false);
        SaveConfig();
        DiagnosticLogInfo("Metric logging stopped from context menu.");
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return;
    }

    std::string selected;
    if (!BrowseForLogFolder(m_hwnd, m_config.log_dir, selected)) {
        DiagnosticLogInfo("Metric logging menu start cancelled: no log folder selected.");
        return;
    }

    m_config.log_dir = selected;
    CreateDirectoryA(selected.c_str(), nullptr);
    if (m_metrics_page) m_metrics_page->SetLogDir(selected);

    if (!StartMetricLoggingSession()) {
        m_config.logging_enabled = false;
        if (m_metrics_page) m_metrics_page->SetLoggingEnabled(false);
        MessageBoxW(m_hwnd,
            L"TelemetryApp could not start logging in that folder. Check write access and the diagnostic logs.",
            L"Metric logging did not start",
            MB_ICONWARNING | MB_OK);
        SaveConfig();
        return;
    }

    m_config.logging_enabled = true;
    if (m_metrics_page) m_metrics_page->SetLoggingEnabled(true);
    SaveConfig();
    DiagnosticLogInfo("Metric logging started from context menu.");
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void AppWindow::HandleMenuCommand(UINT cmd) {
    if (cmd >= TRAY_CMD_DASHBOARD_DEVICE_BASE && cmd <= TRAY_CMD_DASHBOARD_DEVICE_MAX) {
        if (IsFleetHostInstall()) {
            SelectTelemetrySourceByIndex((size_t)(cmd - TRAY_CMD_DASHBOARD_DEVICE_BASE));
        }
        return;
    }

    switch (cmd) {
    case TRAY_CMD_RESTORE:
        DoRestoreFromTray();
        break;
    case TRAY_CMD_HUD:
        ShowWindow(m_hwnd, SW_HIDE);
        m_overlay.Show();
        break;
    case TRAY_CMD_LOGGING_TOGGLE:
        ToggleMetricLoggingFromMenu();
        break;
    case TRAY_CMD_HUD_BOTTOM:
        SetHudPositionFromMenu(HudPosition::AboveTaskbar);
        break;
    case TRAY_CMD_HUD_TOP:
        SetHudPositionFromMenu(HudPosition::Top);
        break;
    case TRAY_CMD_HUD_LEFT:
        SetHudPositionFromMenu(HudPosition::Left);
        break;
    case TRAY_CMD_HUD_RIGHT:
        SetHudPositionFromMenu(HudPosition::Right);
        break;
    case TRAY_CMD_DASHBOARD_THIS_DEVICE:
        SelectTelemetrySourceLocal();
        m_current_page = NavPage::Dashboard;
        if (m_sidebar) m_sidebar->SetPage(NavPage::Dashboard);
        break;
    case TRAY_CMD_MANAGE_FLEET:
        if (IsFleetHostInstall()) {
            m_current_page = NavPage::Fleet;
            if (m_sidebar) m_sidebar->SetPage(NavPage::Fleet);
            ShowWindow(m_hwnd, SW_RESTORE);
            SetForegroundWindow(m_hwnd);
            m_overlay.Hide();
            InvalidateRect(m_hwnd, nullptr, FALSE);
        }
        break;
    case TRAY_CMD_FLEET_METRICS:
        if (IsFleetMetricSelectionReady()) {
            m_current_page = NavPage::Fleet;
            if (m_sidebar) m_sidebar->SetPage(NavPage::Fleet);
            ShowWindow(m_hwnd, SW_RESTORE);
            SetForegroundWindow(m_hwnd);
            m_overlay.Hide();
            InvalidateRect(m_hwnd, nullptr, FALSE);
        } else {
            MessageBoxW(m_hwnd,
                L"Fleet device metric selection is available only when this install is a Fleet Host and the secure fleet service is online.",
                L"Fleet metrics unavailable",
                MB_ICONINFORMATION | MB_OK);
        }
        break;
    case TRAY_CMD_EXIT:
        DestroyWindow(m_hwnd);
        break;
    default:
        break;
    }
}

bool AppWindow::OnMouseDown(float x, float y, int /*btn*/) {
    if (x < Sidebar::WIDTH) {
        m_sidebar->OnClick(x, y);
        InvalidateRect(m_hwnd, nullptr, FALSE);
        return true;
    }
    switch (m_current_page) {
    case NavPage::Dashboard:
        {
        bool was_editing = m_dashboard_page->IsEditMode();
        m_dashboard_page->OnClick(x, y);
        bool save_requested = m_dashboard_page->ConsumeProfileSaveRequested();
        if (save_requested || (!was_editing && !m_dashboard_page->IsEditMode())) {
            m_dashboard_page->FlushToProfile();
            std::string pp = m_profile.FilePath();
            if (!pp.empty()) m_profile.Save(pp);
            m_metric_catalog.BuildFromDashboard(m_profile, m_metric_catalog.HudMetricIds());
            SaveMetricCatalog();
            SyncPagesFromMetricCatalog();
        }
        if (m_dashboard_page->IsEditScrollbarDragging()) SetCapture(m_hwnd);
        }
        break;
    case NavPage::Api:       m_api_page->OnClick(x, y);       break;
    case NavPage::Fleet:
        if (IsFleetHostInstall()) {
            m_fleet_page->OnClick(x, y);
            if (m_fleet_page->IsScrollbarDragging()) SetCapture(m_hwnd);
        }
        break;
    case NavPage::Metrics:
        {
        bool was_logging = m_metrics_page->LoggingEnabled();
        m_metrics_page->OnClick(x, y);
        bool now_logging = m_metrics_page->LoggingEnabled();
        m_config.logging_enabled = m_metrics_page->LoggingEnabled();
        m_config.log_format = m_metrics_page->LogFormat();
        m_config.log_dir = m_metrics_page->LogDir();
        if (!was_logging && now_logging) {
            if (!StartMetricLoggingSession()) {
                m_metrics_page->SetLoggingEnabled(false);
                m_config.logging_enabled = false;
                MessageBoxW(m_hwnd,
                    L"TelemetryApp could not start logging in that folder. Check write access and the diagnostic logs.",
                    L"Metric logging did not start",
                    MB_ICONWARNING | MB_OK);
            }
        } else if (was_logging && !now_logging) {
            StopMetricLoggingSession();
        }
        SyncMetricCatalogFromPages();
        {
            std::string pp = m_profile.FilePath();
            if (!pp.empty()) m_profile.Save(pp);
            SaveMetricCatalog();
        }
        SaveConfig();
        if (m_metrics_page->IsScrollbarDragging()) SetCapture(m_hwnd);
        }
        break;
    case NavPage::Settings:  m_settings_page->OnClick(x, y);  break;
    default: break;
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
    return true;
}

void AppWindow::OnMouseMove(float x, float y) {
    if (m_current_page == NavPage::Dashboard && x >= Sidebar::WIDTH) {
        m_dashboard_page->OnMouseMove(x, y);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    if (m_current_page == NavPage::Metrics && x >= Sidebar::WIDTH) {
        m_metrics_page->OnMouseMove(x, y);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    if (m_current_page == NavPage::Fleet && IsFleetHostInstall() && x >= Sidebar::WIDTH) {
        m_fleet_page->OnMouseMove(x, y);
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
}

void AppWindow::OnMouseUp(float, float) {
    if (m_current_page == NavPage::Dashboard) {
        m_dashboard_page->OnMouseUp();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    if (m_current_page == NavPage::Metrics) {
        m_metrics_page->OnMouseUp();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    if (m_current_page == NavPage::Fleet && IsFleetHostInstall()) {
        m_fleet_page->OnMouseUp();
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }
    if (GetCapture() == m_hwnd) ReleaseCapture();
}

void AppWindow::OnMouseWheel(float delta, float x, float y) {
    if (x < Sidebar::WIDTH) return;
    switch (m_current_page) {
    case NavPage::Dashboard: m_dashboard_page->OnScroll(delta); break;
    case NavPage::Metrics:   m_metrics_page->OnScroll(delta);   break;
    case NavPage::Fleet:     if (IsFleetHostInstall()) m_fleet_page->OnScroll(delta); break;
    default: break;
    }
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void AppWindow::OnChar(wchar_t ch) {
    switch (m_current_page) {
    case NavPage::Api:      m_api_page->OnChar(ch);      break;
    case NavPage::Settings: m_settings_page->OnChar(ch); break;
    default: break;
    }
}

void AppWindow::OnKeyDown(WPARAM vk) {
    if (vk == VK_F5) { OnDataTick(); return; }
    if (GetKeyState(VK_CONTROL) & 0x8000) {
        if (vk == 'E') ExportProfileDialog();
        if (vk == 'I') ImportProfileDialog();
    }
}

// ── Profile import / export ───────────────────────────────────────────────────

void AppWindow::ExportProfileDialog() {
    wchar_t path[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = m_hwnd;
    ofn.lpstrFilter = L"Dashboard Profile (*.json)\0*.json\0All Files\0*.*\0";
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrDefExt = L"json";
    ofn.Flags       = OFN_OVERWRITEPROMPT;
    if (GetSaveFileNameW(&ofn)) {
        std::wstring wp = path;
        SaveProfile(std::string(wp.begin(), wp.end()));
    }
}

void AppWindow::ImportProfileDialog() {
    wchar_t path[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = m_hwnd;
    ofn.lpstrFilter = L"Dashboard Profile (*.json)\0*.json\0All Files\0*.*\0";
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) {
        std::wstring wp = path;
        LoadProfile(std::string(wp.begin(), wp.end()));
    }
}

// ── WndProc ───────────────────────────────────────────────────────────────────

LRESULT CALLBACK AppWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    AppWindow* self = nullptr;
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<AppWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<AppWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    switch (msg) {
    case WM_SIZE:
        if (self) self->OnSize(LOWORD(lp), HIWORD(lp));
        return 0;

    case WM_SYSCOMMAND:
        // Intercept the minimize button/key — route through our behavior switch
        if (self && (wp & 0xFFF0) == SC_MINIMIZE) {
            self->DoMinimize();
            return 0; // prevent default minimize
        }
        break;

    case WM_TIMER:
        if (self) {
            switch (wp) {
            case RENDER_TIMER: self->OnRender();   break;
            case DATA_TIMER:   self->OnDataTick(); break;
            case API_TIMER:    self->OnApiTick();  break;
            }
        }
        return 0;

    case WM_LBUTTONDOWN:
        if (self) self->OnMouseDown((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp), 0);
        return 0;
    case WM_MOUSEMOVE:
        if (self) self->OnMouseMove((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp));
        return 0;
    case WM_LBUTTONUP:
        if (self) self->OnMouseUp((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp));
        return 0;
    case WM_RBUTTONDOWN:
        if (self) self->OnMouseDown((float)GET_X_LPARAM(lp), (float)GET_Y_LPARAM(lp), 1);
        return 0;

    case WM_MOUSEWHEEL: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &pt);
        float delta = (float)GET_WHEEL_DELTA_WPARAM(wp) / 120.0f;
        if (self) self->OnMouseWheel(delta, (float)pt.x, (float)pt.y);
        return 0;
    }

    case WM_CHAR:
        if (self) self->OnChar((wchar_t)wp);
        return 0;
    case WM_KEYDOWN:
        if (self) self->OnKeyDown(wp);
        return 0;

    // System tray callback
    case WM_TRAY:
        if (self) {
            bool logging_enabled = self->m_metrics_page && self->m_metrics_page->LoggingEnabled();
            int selected_remote = -1;
            auto options = self->BuildTrayDeviceOptions(selected_remote);
            SystemTray::HandleMessage(hwnd, lp, logging_enabled,
                static_cast<int>(self->m_config.hud_position),
                self->IsFleetHostInstall(),
                self->IsFleetMetricSelectionReady(),
                options,
                selected_remote,
                [self](UINT cmd) { self->HandleMenuCommand(cmd); });
        }
        return 0;

    // Right-click on overlay restores (handled in overlay's own WndProc; this
    // covers any WM_COMMAND the menu posts to the owner window)
    case WM_COMMAND:
        if (self) {
            self->HandleMenuCommand(LOWORD(wp));
            return 0;
        }
        break;

    case WM_DESTROY:
        if (self) self->Destroy();
        PostQuitMessage(0);
        return 0;
    }

    if (self) {
        static UINT s_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
        if (msg == s_taskbar_created) {
            self->m_tray.Remove();
            self->EnsureTrayIcon();
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void AppWindow::OnSize(UINT w, UINT h) {
    if (w == 0 || h == 0) return;
    m_width = w; m_height = h;
    if (!m_d2d.DC()) return;
    m_d2d.Resize(w, h);
}

} // namespace Client
