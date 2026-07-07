#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <functional>

namespace Client {

// WM_TRAY fires on HWND owner when user interacts with the tray icon.
// Call HandleMessage(owner_hwnd, lp, callback) from WndProc on WM_TRAY.
static constexpr UINT WM_TRAY = WM_USER + 100;

static constexpr UINT TRAY_CMD_RESTORE = 1;
static constexpr UINT TRAY_CMD_HUD     = 2;
static constexpr UINT TRAY_CMD_EXIT    = 3;
static constexpr UINT TRAY_CMD_LOGGING_TOGGLE = 4;
static constexpr UINT TRAY_CMD_HUD_BOTTOM = 10;
static constexpr UINT TRAY_CMD_HUD_TOP    = 11;
static constexpr UINT TRAY_CMD_HUD_LEFT   = 12;
static constexpr UINT TRAY_CMD_HUD_RIGHT  = 13;
static constexpr UINT TRAY_CMD_FLEET_METRICS = 20;

class SystemTray {
public:
    SystemTray() = default;
    ~SystemTray() { Remove(); }

    bool Add(HWND owner_hwnd, HICON icon, const wchar_t* tooltip);
    void Remove();
    void SetTooltip(const wchar_t* tooltip);
    void ShowContextMenu();

    // Call from WndProc when msg == WM_TRAY
    static void HandleMessage(HWND owner_hwnd, LPARAM lp, bool logging_enabled,
                              int hud_position, bool fleet_visible, bool fleet_ready,
                              const std::function<void(UINT)>& on_cmd);

    bool IsAdded() const { return m_added; }

private:
    HWND           m_owner = nullptr;
    NOTIFYICONDATAW m_nid  = {};
    bool           m_added = false;
};

} // namespace Client
