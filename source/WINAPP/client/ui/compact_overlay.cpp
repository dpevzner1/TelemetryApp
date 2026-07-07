#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <dwrite.h>
#include <shellapi.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include "compact_overlay.h"
#include "system_tray.h"
#include "../ipc/shm_reader.h"
#include "../../shared/metric_ids.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "shell32.lib")

namespace Client {

namespace {

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (needed <= 0) return std::wstring(s.begin(), s.end());
    std::wstring out(static_cast<size_t>(needed - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), needed);
    return out;
}

HMENU BuildHudContextMenu(HudPosition position, bool fleet_visible) {
    int pos = static_cast<int>(position);
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, TRAY_CMD_RESTORE, L"Restore");
    AppendMenuW(menu, MF_STRING, TRAY_CMD_LOGGING_TOGGLE, L"Start / Stop Capturing Logs / Metrics");

    HMENU hud_menu = CreatePopupMenu();
    AppendMenuW(hud_menu, MF_STRING | (pos == 0 ? MF_CHECKED : 0),
                TRAY_CMD_HUD_BOTTOM, L"Above Taskbar");
    AppendMenuW(hud_menu, MF_STRING | (pos == 1 ? MF_CHECKED : 0),
                TRAY_CMD_HUD_TOP, L"Top Edge");
    AppendMenuW(hud_menu, MF_STRING | (pos == 2 ? MF_CHECKED : 0),
                TRAY_CMD_HUD_LEFT, L"Left Edge");
    AppendMenuW(hud_menu, MF_STRING | (pos == 3 ? MF_CHECKED : 0),
                TRAY_CMD_HUD_RIGHT, L"Right Edge");
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)hud_menu, L"HUD Orientation");

    if (fleet_visible) {
        AppendMenuW(menu, MF_STRING, TRAY_CMD_FLEET_METRICS, L"Fleet Device Metrics...");
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, TRAY_CMD_EXIT, L"Exit");
    return menu;
}

}

// ── Default metrics (Core + Thermal + I/O) ────────────────────────────────────

std::vector<HudMetric> MakeDefaultHudMetrics() {
    return {
        {MetricId::CPU_USAGE_TOTAL,      "CPU",   "%",    0.70f, 0.90f, 100.0f},
        {MetricId::MEM_PERCENT,          "RAM",   "%",    0.75f, 0.90f, 100.0f},
        {MetricId::CPU_PACKAGE_TEMP_C,   "CPU°",  "°C",   0.70f, 0.85f,  100.0f},
        // GPU 0 metrics
        {gpu_metric(0, GpuOff::USAGE_PCT),"GPU",  "%",    0.70f, 0.90f, 100.0f},
        {gpu_metric(0, GpuOff::VRAM_PCT), "VRAM", "%",    0.80f, 0.95f, 100.0f},
        {gpu_metric(0, GpuOff::TEMP_C),   "GPU°", "°C",   0.70f, 0.85f, 100.0f},
        // Disk 0 read/write
        {disk_metric(0, DiskOff::READ_BYTES_S),  "DR", "MB/s", 0, 0, 2000.0f},
        {disk_metric(0, DiskOff::WRITE_BYTES_S), "DW", "MB/s", 0, 0, 2000.0f},
        // NIC 0 recv/send
        {net_metric(0, NetOff::RECV_BYTES_S), "↓", "MB/s", 0, 0, 1000.0f},
        {net_metric(0, NetOff::SENT_BYTES_S), "↑", "MB/s", 0, 0, 1000.0f},
    };
}

// ── Helpers ───────────────────────────────────────────────────────────────────

void CompactOverlay::GetWorkAreaAndTaskbar(RECT& work_area, RECT& taskbar) {
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);
    // Taskbar occupies the gap between screen and work area (assumes taskbar at bottom)
    taskbar.left   = 0;
    taskbar.top    = work_area.bottom;
    taskbar.right  = GetSystemMetrics(SM_CXSCREEN);
    taskbar.bottom = GetSystemMetrics(SM_CYSCREEN);
}

void CompactOverlay::ComputeBarRect() {
    RECT wa{}, tb{};
    GetWorkAreaAndTaskbar(wa, tb);
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);

    switch (m_position) {
    case HudPosition::AboveTaskbar:
        m_bar_rect = {0, wa.bottom - HUD_THICKNESS, sw, wa.bottom};
        break;
    case HudPosition::Top:
        m_bar_rect = {0, 0, sw, HUD_THICKNESS};
        break;
    case HudPosition::Left:
        m_bar_rect = {0, 0, HUD_THICKNESS, sh};
        break;
    case HudPosition::Right:
        m_bar_rect = {sw - HUD_THICKNESS, 0, sw, sh};
        break;
    }
}

void CompactOverlay::RegisterAppBar() {
    if (!m_hwnd || m_appbar_registered) return;

    APPBARDATA abd{};
    abd.cbSize = sizeof(abd);
    abd.hWnd = m_hwnd;
    abd.uCallbackMessage = WM_APP + 42;
    if (SHAppBarMessage(ABM_NEW, &abd)) {
        m_appbar_registered = true;
    }
}

void CompactOverlay::UnregisterAppBar() {
    if (!m_hwnd || !m_appbar_registered) return;

    APPBARDATA abd{};
    abd.cbSize = sizeof(abd);
    abd.hWnd = m_hwnd;
    SHAppBarMessage(ABM_REMOVE, &abd);
    m_appbar_registered = false;
}

void CompactOverlay::ApplyAppBarPosition() {
    if (!m_hwnd) return;
    RegisterAppBar();
    ComputeBarRect();

    APPBARDATA abd{};
    abd.cbSize = sizeof(abd);
    abd.hWnd = m_hwnd;
    abd.rc = m_bar_rect;

    switch (m_position) {
    case HudPosition::Top:
        abd.uEdge = ABE_TOP;
        abd.rc.bottom = abd.rc.top + HUD_THICKNESS;
        break;
    case HudPosition::Left:
        abd.uEdge = ABE_LEFT;
        abd.rc.right = abd.rc.left + HUD_THICKNESS;
        break;
    case HudPosition::Right:
        abd.uEdge = ABE_RIGHT;
        abd.rc.left = abd.rc.right - HUD_THICKNESS;
        break;
    case HudPosition::AboveTaskbar:
    default:
        abd.uEdge = ABE_BOTTOM;
        abd.rc.top = abd.rc.bottom - HUD_THICKNESS;
        break;
    }

    SHAppBarMessage(ABM_QUERYPOS, &abd);
    switch (m_position) {
    case HudPosition::Top:
        abd.rc.bottom = abd.rc.top + HUD_THICKNESS;
        break;
    case HudPosition::Left:
        abd.rc.right = abd.rc.left + HUD_THICKNESS;
        break;
    case HudPosition::Right:
        abd.rc.left = abd.rc.right - HUD_THICKNESS;
        break;
    case HudPosition::AboveTaskbar:
    default:
        abd.rc.top = abd.rc.bottom - HUD_THICKNESS;
        break;
    }
    SHAppBarMessage(ABM_SETPOS, &abd);
    m_bar_rect = abd.rc;
}

// ── Window class + Create ─────────────────────────────────────────────────────

bool CompactOverlay::Create(HINSTANCE hinstance, HWND owner_hwnd) {
    m_owner_hwnd = owner_hwnd;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hinstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassExW(&wc); // ok if already registered

    ComputeBarRect();

    m_hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        CLASS_NAME, L"TelemetryApp HUD",
        WS_POPUP,
        m_bar_rect.left, m_bar_rect.top,
        m_bar_rect.right - m_bar_rect.left,
        m_bar_rect.bottom - m_bar_rect.top,
        nullptr, nullptr, hinstance, this);

    if (!m_hwnd) return false;

    // Slight transparency for a sleek look (95% opaque)
    SetLayeredWindowAttributes(m_hwnd, 0, 242, LWA_ALPHA);

    if (!InitD2D()) return false;

    m_values.assign(m_metrics.size(), 0.0f);

    // 1Hz render timer on the overlay HWND
    SetTimer(m_hwnd, 1, 1000, nullptr);
    return true;
}

bool CompactOverlay::InitD2D() {
    D2D1_FACTORY_OPTIONS opts{};
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, opts, m_d2d_factory.GetAddressOf());

    RECT r{};
    GetClientRect(m_hwnd, &r);
    D2D1_SIZE_U sz = {(UINT)(r.right - r.left), (UINT)(r.bottom - r.top)};

    D2D1_RENDER_TARGET_PROPERTIES rtp = D2D1::RenderTargetProperties();
    D2D1_HWND_RENDER_TARGET_PROPERTIES hwrtp = D2D1::HwndRenderTargetProperties(m_hwnd, sz);
    HRESULT hr = m_d2d_factory->CreateHwndRenderTarget(rtp, hwrtp, &m_rt);
    if (FAILED(hr)) return false;

    m_rt->CreateSolidColorBrush({1,1,1,1}, &m_br);

    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(m_dw_factory.GetAddressOf()));
    m_dw_factory->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        10.0f, L"en-us", &m_fmt_label);
    m_dw_factory->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        13.0f, L"en-us", &m_fmt_value);
    if (m_fmt_label) m_fmt_label->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    if (m_fmt_value) m_fmt_value->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    return true;
}

void CompactOverlay::Destroy() {
    if (m_hwnd) {
        UnregisterAppBar();
        KillTimer(m_hwnd, 1);
        m_fmt_value.Reset(); m_fmt_label.Reset();
        m_dw_factory.Reset(); m_br.Reset();
        m_rt.Reset(); m_d2d_factory.Reset();
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

// ── Show / Hide ───────────────────────────────────────────────────────────────

void CompactOverlay::Show() {
    if (!m_hwnd) return;
    ApplyAppBarPosition();
    SetWindowPos(m_hwnd, HWND_TOPMOST,
        m_bar_rect.left, m_bar_rect.top,
        m_bar_rect.right - m_bar_rect.left,
        m_bar_rect.bottom - m_bar_rect.top,
        SWP_SHOWWINDOW | SWP_NOACTIVATE);
    Render();
}

void CompactOverlay::Hide() {
    if (m_hwnd) {
        UnregisterAppBar();
        ShowWindow(m_hwnd, SW_HIDE);
    }
}

bool CompactOverlay::IsVisible() const {
    return m_hwnd && IsWindowVisible(m_hwnd);
}

void CompactOverlay::SetPosition(HudPosition pos) {
    m_position = pos;
    if (IsVisible()) {
        UnregisterAppBar();
        Show(); // reposition immediately
    }
}

void CompactOverlay::UpdateValue(uint32_t metric_id, float value) {
    for (size_t i = 0; i < m_metrics.size(); ++i)
        if (m_metrics[i].metric_id == metric_id) { m_values[i] = value; return; }
}

// ── Render ────────────────────────────────────────────────────────────────────

void CompactOverlay::Render() {
    if (!m_rt || !IsVisible()) return;

    // Refresh values from SHM directly
    for (size_t i = 0; i < m_metrics.size() && i < m_values.size(); ++i) {
        double v = 0;
        ShmReadMetric(m_metrics[i].metric_id, v);
        m_values[i] = (float)v;
    }

    m_rt->BeginDraw();
    m_rt->Clear({0.09f, 0.09f, 0.11f, 1.0f});

    bool vertical = (m_position == HudPosition::Left || m_position == HudPosition::Right);
    if (vertical) DrawVertical();
    else          DrawHorizontal();
    DrawTooltip();

    m_rt->EndDraw();
}

int CompactOverlay::HitTestMetric(int x, int y) const {
    RECT r{};
    GetClientRect(m_hwnd, &r);
    int n = (int)m_metrics.size();
    if (n <= 0) return -1;
    bool vertical = (m_position == HudPosition::Left || m_position == HudPosition::Right);
    if (vertical) {
        float cell_h = (float)(r.bottom - r.top) / n;
        int idx = (int)(y / std::max(cell_h, 1.0f));
        return (idx >= 0 && idx < n) ? idx : -1;
    }
    float cell_w = ((float)(r.right - r.left) - 8.0f) / n;
    int idx = (int)((x - 8.0f) / std::max(cell_w, 1.0f));
    return (idx >= 0 && idx < n) ? idx : -1;
}

void CompactOverlay::DrawTooltip() {
    if (m_hover_index < 0 || m_hover_index >= (int)m_metrics.size()) return;
    const auto& m = m_metrics[m_hover_index];
    float val = (m_hover_index < (int)m_values.size()) ? m_values[m_hover_index] : 0.0f;

    RECT r{};
    GetClientRect(m_hwnd, &r);
    float w = 250.0f;
    float h = 58.0f;
    float x = (float)m_hover_x + 12.0f;
    float y = (float)m_hover_y + 8.0f;
    if (x + w > r.right) x = (float)m_hover_x - w - 12.0f;
    if (y + h > r.bottom) y = (float)m_hover_y - h - 8.0f;
    x = std::max(4.0f, x);
    y = std::max(4.0f, y);

    D2D1_RECT_F bg = {x, y, x + w, y + h};
    m_br->SetColor({0.04f, 0.04f, 0.05f, 0.96f});
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(bg, 5, 5), m_br.Get());
    m_br->SetColor({0.95f, 0.24f, 0.24f, 1.0f});
    m_rt->DrawRoundedRectangle(D2D1::RoundedRect(bg, 5, 5), m_br.Get(), 1.0f);

    char line1[96]{};
    snprintf(line1, sizeof(line1), "%s: %.2f %s", m.label.c_str(), val, m.unit.c_str());
    std::wstring w1 = Utf8ToWide(line1);
    m_br->SetColor({0.96f, 0.96f, 0.96f, 1.0f});
    m_rt->DrawText(w1.c_str(), (UINT32)w1.size(), m_fmt_value.Get(),
        {x + 8, y + 6, x + w - 8, y + 25}, m_br.Get());

    char line2[160]{};
    snprintf(line2, sizeof(line2), "Metric ID %u. Warn %.0f%%, critical %.0f%% of %.0f %s.",
        m.metric_id, m.warn_pct * 100.0f, m.crit_pct * 100.0f, m.y_max, m.unit.c_str());
    std::wstring w2 = Utf8ToWide(line2);
    m_br->SetColor({0.70f, 0.70f, 0.74f, 1.0f});
    m_rt->DrawText(w2.c_str(), (UINT32)w2.size(), m_fmt_label.Get(),
        {x + 8, y + 28, x + w - 8, y + h - 6}, m_br.Get());
}

void CompactOverlay::DrawHorizontal() {
    RECT r{};
    GetClientRect(m_hwnd, &r);
    float h = (float)(r.bottom - r.top);
    float w = (float)(r.right - r.left);

    // App brand strip (left)
    m_br->SetColor({0.25f, 0.60f, 1.00f, 1.0f});
    m_rt->FillRectangle({0, 0, 4, h}, m_br.Get());

    // Metric cells
    int n = (int)m_metrics.size();
    float cell_w = (w - 8.0f) / std::max(n, 1);
    float x = 8.0f;

    for (int i = 0; i < n; ++i) {
        const auto& m = m_metrics[i];
        float val   = (i < (int)m_values.size()) ? m_values[i] : 0.0f;
        float frac  = m.y_max > 0 ? val / m.y_max : 0;
        frac = std::clamp(frac, 0.0f, 1.0f);

        // Color: green → amber → red based on warn/crit thresholds
        D2D1_COLOR_F vcol = {0.80f, 0.90f, 0.85f, 1.0f};
        if (m.crit_pct > 0 && frac >= m.crit_pct)
            vcol = {1.0f, 0.30f, 0.25f, 1.0f};
        else if (m.warn_pct > 0 && frac >= m.warn_pct)
            vcol = {1.0f, 0.70f, 0.15f, 1.0f};

        // Thin fill bar at bottom of cell
        float bar_y = h - 3.0f;
        m_br->SetColor({0.18f, 0.18f, 0.22f, 1.0f});
        m_rt->FillRectangle({x, bar_y, x + cell_w - 2, h}, m_br.Get());
        m_br->SetColor(vcol);
        m_rt->FillRectangle({x, bar_y, x + (cell_w - 2) * frac, h}, m_br.Get());

        // Label
        m_br->SetColor({0.45f, 0.45f, 0.50f, 1.0f});
        std::wstring wl = Utf8ToWide(m.label);
        m_rt->DrawText(wl.c_str(), (UINT32)wl.size(), m_fmt_label.Get(),
            {x, 2.0f, x + cell_w - 2, 14.0f}, m_br.Get());

        // Value
        char buf[24]{};
        if (m.unit == "MB/s" || m.unit == "KB/s")
            snprintf(buf, sizeof(buf), "%.0f", val);
        else if (m.unit == "°C")
            snprintf(buf, sizeof(buf), "%.0f°", val);
        else
            snprintf(buf, sizeof(buf), "%.0f%%", val);
        std::wstring wv = Utf8ToWide(buf);
        m_br->SetColor(vcol);
        m_rt->DrawText(wv.c_str(), (UINT32)wv.size(), m_fmt_value.Get(),
            {x, 13.0f, x + cell_w - 2, h - 5.0f}, m_br.Get());

        // Separator
        if (i < n - 1) {
            m_br->SetColor({0.22f, 0.22f, 0.28f, 1.0f});
            m_rt->DrawLine({x + cell_w - 1, 4}, {x + cell_w - 1, h - 4}, m_br.Get(), 0.5f);
        }
        x += cell_w;
    }

    // Right: "click to restore" hint
    m_br->SetColor({0.30f, 0.30f, 0.35f, 1.0f});
    m_rt->DrawText(L"▲ click to restore", 18, m_fmt_label.Get(),
        {w - 120.0f, 2.0f, w - 4.0f, h - 4.0f}, m_br.Get());

    // Top border line
    m_br->SetColor({0.25f, 0.60f, 1.00f, 0.4f});
    m_rt->DrawLine({0, 0.5f}, {w, 0.5f}, m_br.Get(), 1.0f);
}

void CompactOverlay::DrawVertical() {
    RECT r{};
    GetClientRect(m_hwnd, &r);
    float w = (float)(r.right - r.left);
    float h = (float)(r.bottom - r.top);

    m_rt->Clear({0.09f, 0.09f, 0.11f, 1.0f});

    int n = (int)m_metrics.size();
    float cell_h = h / std::max(n, 1);
    float y = 0.0f;

    for (int i = 0; i < n; ++i) {
        const auto& m = m_metrics[i];
        float val   = (i < (int)m_values.size()) ? m_values[i] : 0.0f;
        float frac  = m.y_max > 0 ? val / m.y_max : 0;
        frac = std::clamp(frac, 0.0f, 1.0f);

        D2D1_COLOR_F vcol = {0.80f, 0.90f, 0.85f, 1.0f};
        if (m.crit_pct > 0 && frac >= m.crit_pct)       vcol = {1.0f, 0.30f, 0.25f, 1.0f};
        else if (m.warn_pct > 0 && frac >= m.warn_pct)  vcol = {1.0f, 0.70f, 0.15f, 1.0f};

        // Fill bar on the left edge
        m_br->SetColor({0.18f, 0.18f, 0.22f, 1.0f});
        m_rt->FillRectangle({0, y, 3, y + cell_h - 2}, m_br.Get());
        m_br->SetColor(vcol);
        m_rt->FillRectangle({0, y + (cell_h - 2) * (1.0f - frac), 3, y + cell_h - 2}, m_br.Get());

        char buf[12]{};
        snprintf(buf, sizeof(buf), "%.0f", val);
        std::wstring wv = Utf8ToWide(buf);
        m_br->SetColor(vcol);
        // Rotated text not supported directly in D2D without transform — draw horizontally
        m_rt->DrawText(wv.c_str(), (UINT32)wv.size(), m_fmt_label.Get(),
            {4, y + 2, w, y + cell_h - 2}, m_br.Get());

        y += cell_h;
    }
}

// ── WndProc ───────────────────────────────────────────────────────────────────

LRESULT CALLBACK CompactOverlay::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    CompactOverlay* self = nullptr;
    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<CompactOverlay*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<CompactOverlay*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    switch (msg) {
    case WM_APP + 42:
        if (self && self->IsVisible()) {
            self->ApplyAppBarPosition();
            SetWindowPos(self->m_hwnd, HWND_TOPMOST,
                self->m_bar_rect.left, self->m_bar_rect.top,
                self->m_bar_rect.right - self->m_bar_rect.left,
                self->m_bar_rect.bottom - self->m_bar_rect.top,
                SWP_NOACTIVATE);
            self->Render();
        }
        return 0;

    case WM_TIMER:
        if (wp == 1 && self) self->Render();
        return 0;

    case WM_MOUSEMOVE:
        if (self) {
            self->m_hover_x = GET_X_LPARAM(lp);
            self->m_hover_y = GET_Y_LPARAM(lp);
            self->m_hover_index = self->HitTestMetric(self->m_hover_x, self->m_hover_y);
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            self->Render();
        }
        return 0;

    case WM_MOUSELEAVE:
        if (self) {
            self->m_hover_index = -1;
            self->Render();
        }
        return 0;

    case WM_LBUTTONDOWN:
        // Click on bar → restore main window
        if (self && self->m_owner_hwnd) {
            ShowWindow(self->m_owner_hwnd, SW_RESTORE);
            SetForegroundWindow(self->m_owner_hwnd);
            self->Hide();
        }
        return 0;

    case WM_RBUTTONUP:
        if (self && self->m_owner_hwnd) {
            HMENU menu = BuildHudContextMenu(self->m_position, self->m_fleet_menu_visible);
            POINT pt{};
            GetCursorPos(&pt);
            SetForegroundWindow(self->m_owner_hwnd);
            UINT cmd = (UINT)TrackPopupMenu(menu,
                TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN,
                pt.x, pt.y, 0, self->m_owner_hwnd, nullptr);
            PostMessageW(self->m_owner_hwnd, WM_NULL, 0, 0);
            DestroyMenu(menu);
            if (cmd) PostMessageW(self->m_owner_hwnd, WM_COMMAND, cmd, 0);
        }
        return 0;

    case WM_SIZE:
        if (self && self->m_rt) {
            UINT w = LOWORD(lp), h = HIWORD(lp);
            self->m_rt->Resize(D2D1::SizeU(w, h));
        }
        return 0;

    case WM_DESTROY:
        return 0;

    case WM_SETCURSOR:
        SetCursor(LoadCursor(nullptr, IDC_HAND));
        return TRUE;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace Client
