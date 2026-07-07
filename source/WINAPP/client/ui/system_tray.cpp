#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include "system_tray.h"

namespace Client {

namespace {

HMENU BuildContextMenu(bool logging_enabled, int hud_position, bool fleet_visible, bool fleet_ready) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, TRAY_CMD_RESTORE, L"Restore");
    AppendMenuW(menu, MF_STRING, TRAY_CMD_HUD, L"Show HUD Bar");
    AppendMenuW(menu, MF_STRING, TRAY_CMD_LOGGING_TOGGLE,
                logging_enabled ? L"Stop Capturing Logs / Metrics" : L"Start Capturing Logs / Metrics");

    HMENU hud_menu = CreatePopupMenu();
    AppendMenuW(hud_menu, MF_STRING | (hud_position == 0 ? MF_CHECKED : 0),
                TRAY_CMD_HUD_BOTTOM, L"Above Taskbar");
    AppendMenuW(hud_menu, MF_STRING | (hud_position == 1 ? MF_CHECKED : 0),
                TRAY_CMD_HUD_TOP, L"Top Edge");
    AppendMenuW(hud_menu, MF_STRING | (hud_position == 2 ? MF_CHECKED : 0),
                TRAY_CMD_HUD_LEFT, L"Left Edge");
    AppendMenuW(hud_menu, MF_STRING | (hud_position == 3 ? MF_CHECKED : 0),
                TRAY_CMD_HUD_RIGHT, L"Right Edge");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)hud_menu, L"HUD Orientation");

    if (fleet_visible) {
        AppendMenuW(menu, fleet_ready ? MF_STRING : MF_GRAYED,
                    TRAY_CMD_FLEET_METRICS, L"Fleet Device Metrics...");
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, TRAY_CMD_EXIT, L"Exit");
    return menu;
}

} // namespace

bool SystemTray::Add(HWND owner_hwnd, HICON icon, const wchar_t* tooltip) {
    m_owner = owner_hwnd;
    m_nid             = {};
    m_nid.cbSize      = sizeof(m_nid);
    m_nid.hWnd        = owner_hwnd;
    m_nid.uID         = 1;
    m_nid.uFlags      = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAY;
    m_nid.hIcon       = icon;
    wcsncpy_s(m_nid.szTip, tooltip, _TRUNCATE);
    m_added = (Shell_NotifyIconW(NIM_ADD, &m_nid) != FALSE);
    return m_added;
}

void SystemTray::Remove() {
    if (m_added) {
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        m_added = false;
    }
}

void SystemTray::SetTooltip(const wchar_t* tooltip) {
    if (!m_added) return;
    wcsncpy_s(m_nid.szTip, tooltip, _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}

void SystemTray::ShowContextMenu() {
    HMENU menu = BuildContextMenu(false, 0, false, false);
    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(m_owner);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                   pt.x, pt.y, 0, m_owner, nullptr);
    PostMessageW(m_owner, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

void SystemTray::HandleMessage(HWND owner_hwnd, LPARAM lp, bool logging_enabled,
                               int hud_position, bool fleet_visible, bool fleet_ready,
                               const std::function<void(UINT)>& on_cmd) {
    switch (LOWORD(lp)) {
    case WM_LBUTTONDBLCLK:
        on_cmd(TRAY_CMD_RESTORE);
        break;
    case WM_RBUTTONUP: {
        HMENU menu = BuildContextMenu(logging_enabled, hud_position, fleet_visible, fleet_ready);
        POINT pt{};
        GetCursorPos(&pt);
        SetForegroundWindow(owner_hwnd);
        UINT cmd = (UINT)TrackPopupMenu(menu,
            TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
            pt.x, pt.y, 0, owner_hwnd, nullptr);
        PostMessageW(owner_hwnd, WM_NULL, 0, 0);
        DestroyMenu(menu);
        if (cmd) on_cmd(cmd);
        break;
    }
    }
}

} // namespace Client
