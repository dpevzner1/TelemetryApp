#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <vector>
#include <string>
#include <cstdint>
#include "system_tray.h"

using Microsoft::WRL::ComPtr;

namespace Client {

// Which metrics appear in the HUD bar, in order.
struct HudMetric {
    uint32_t    metric_id;
    std::string label;      // short label e.g. "CPU"
    std::string unit;       // "%" | "C" | "MB/s"
    float       warn_pct;   // fraction of y_max that triggers amber (0=disabled)
    float       crit_pct;   // fraction of y_max that triggers red   (0=disabled)
    float       y_max;
};

enum class HudPosition : int {
    AboveTaskbar = 0,   // default: just above Windows taskbar
    Top          = 1,   // top of screen (pushes work area down)
    Left         = 2,   // left edge, vertical
    Right        = 3,   // right edge, vertical
};

// HUD bar height for horizontal layouts; width for vertical layouts
static constexpr int HUD_THICKNESS = 36;

class CompactOverlay {
public:
    CompactOverlay() = default;
    ~CompactOverlay() { Destroy(); }

    bool Create(HINSTANCE hinstance, HWND owner_hwnd);
    void Destroy();

    void Show();
    void Hide();
    bool IsVisible() const;

    void SetMetrics(std::vector<HudMetric> metrics) { m_metrics = std::move(metrics); }
    std::vector<HudMetric>& Metrics() { return m_metrics; }

    void SetPosition(HudPosition pos);
    HudPosition Position() const { return m_position; }
    void SetFleetMenuVisible(bool visible) { m_fleet_menu_visible = visible; }
    void SetRecordingActive(bool active) { m_recording_active = active; }
    void SetFleetDeviceOptions(std::vector<TrayDeviceOption> devices, int selected_source_index) {
        m_fleet_devices = std::move(devices);
        m_selected_source_index = selected_source_index;
    }

    // Push a live value for a metric (call from data tick)
    void UpdateValue(uint32_t metric_id, float value);

    // Render one frame — called from a 1Hz timer on the overlay HWND
    void Render();

    // HWND accessor (for message routing)
    HWND Hwnd() const { return m_hwnd; }

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static constexpr wchar_t CLASS_NAME[] = L"TelemetryHUDOverlay";

private:
    HWND    m_hwnd       = nullptr;
    HWND    m_owner_hwnd = nullptr;

    // D2D — own device context (separate from main window)
    ComPtr<ID2D1Factory1>       m_d2d_factory;
    ComPtr<ID2D1HwndRenderTarget> m_rt;
    ComPtr<IDWriteFactory>      m_dw_factory;
    ComPtr<IDWriteTextFormat>   m_fmt_label;
    ComPtr<IDWriteTextFormat>   m_fmt_value;
    ComPtr<ID2D1SolidColorBrush> m_br;

    HudPosition              m_position = HudPosition::AboveTaskbar;
    std::vector<HudMetric>  m_metrics;
    std::vector<float>       m_values;   // parallel to m_metrics

    RECT m_bar_rect{};  // screen coordinates of the bar window
    RECT m_last_appbar_rect{};
    bool m_appbar_registered = false;
    bool m_appbar_applying = false;
    bool m_appbar_reposition_pending = false;
    bool m_fleet_menu_visible = false;
    bool m_recording_active = false;
    std::vector<TrayDeviceOption> m_fleet_devices;
    int  m_selected_source_index = -1;
    int  m_hover_index = -1;
    int  m_hover_x = 0;
    int  m_hover_y = 0;

    bool InitD2D();
    void ComputeBarRect();
    void RegisterAppBar();
    void UnregisterAppBar();
    void ApplyAppBarPosition();
    void DrawHorizontal();
    void DrawVertical();
    void DrawTooltip();
    int HitTestMetric(int x, int y) const;

    static void GetScreenAndTaskbar(RECT& screen, RECT& taskbar);
};

// Default HUD metric set (Core + Thermal + I/O)
std::vector<HudMetric> MakeDefaultHudMetrics();

} // namespace Client
