#pragma once
#include "../renderer/d2d_context.h"
#include "../config/dashboard_profile.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cfloat>

namespace Client {

// Metric descriptor for the logging selection tree
struct MetricDesc {
    uint32_t    id;
    std::string label;
    std::string cluster;
    std::string unit;
    bool        logged = true;
};

// Logical logging groups (DevOps recommendation: functional, not dashboard-visual)
enum class LogGroup : int {
    HighFreqNumerics = 0, // CPU, GPU, clock, net bytes  (~1s)
    SlowState        = 1, // temps, VRAM%, free%          (~10s)
    EventDriven      = 2, // throttle, errors, drops      (on-change)
    ProcessWatcher   = 3, // per-process opt-in
    SelfMonitoring   = 4, // always on
};

struct LogGroupEntry {
    LogGroup    group;
    std::string label;
    std::string description;
    bool        enabled         = true;
    int         interval_sec    = 1;  // for high-freq and slow-state
    bool        on_change_only  = false; // for event-driven
};

class MetricsPage {
public:
    explicit MetricsPage(D2DContext& ctx);

    void SetMetrics(std::vector<MetricDesc> metrics);

    // Sync current state from/to profile
    void SyncFromProfile(const std::vector<MetricPanel>& panels);
    std::vector<uint32_t> GetLoggedMetricIds() const;

    // Log group settings
    const std::vector<LogGroupEntry>& Groups() const { return m_groups; }
    void SetGroupEnabled(LogGroup g, bool enabled);

    // Output format
    std::string LogFormat() const { return m_log_format; }
    void SetLogFormat(const std::string& f) { m_log_format = f; }

    // Log file path
    std::string LogDir() const { return m_log_dir; }
    void SetLogDir(const std::string& d) { m_log_dir = d; }

    bool LoggingEnabled() const { return m_logging_enabled; }
    void SetLoggingEnabled(bool enabled) { m_logging_enabled = enabled; }

    void SetHudMetricIds(const std::vector<uint32_t>& ids);
    std::vector<uint32_t> GetHudMetricIds() const;

    void SetOnLoggingEnableRequested(std::function<bool(MetricsPage&)> cb) {
        m_on_logging_enable_requested = std::move(cb);
    }
    void SetOnHudMetricsChanged(std::function<void(const std::vector<uint32_t>&)> cb) {
        m_on_hud_metrics_changed = std::move(cb);
    }

    void Draw(float x, float y, float w, float h, float dpi_scale);
    void OnClick(float x, float y);
    void OnMouseMove(float x, float y);
    void OnMouseUp();
    void OnScroll(float delta);
    bool IsScrollbarDragging() const { return m_scroll_dragging; }

private:
    D2DContext& m_ctx;
    std::vector<MetricDesc>    m_metrics;
    std::vector<LogGroupEntry> m_groups;
    std::string m_log_format = "jsonl";
    std::string m_log_dir;
    bool        m_logging_enabled = false;
    float       m_scroll_y = 0;
    float       m_content_h = 0;
    float       m_view_h = 0;
    float       m_view_y = 0;
    float       m_page_x = 0;
    float       m_page_y = 0;
    float       m_page_w = 0;
    float       m_page_h = 0;
    bool        m_hud_all = false;
    bool        m_scroll_dragging = false;
    float       m_scroll_drag_offset = 0.0f;
    float       m_scroll_rail_x0 = 0.0f;
    float       m_scroll_rail_y0 = 0.0f;
    float       m_scroll_rail_x1 = 0.0f;
    float       m_scroll_rail_y1 = 0.0f;
    float       m_scroll_thumb_x0 = 0.0f;
    float       m_scroll_thumb_y0 = 0.0f;
    float       m_scroll_thumb_x1 = 0.0f;
    float       m_scroll_thumb_y1 = 0.0f;

    // Cluster expand/collapse
    std::unordered_map<std::string, bool> m_cluster_expanded;
    std::unordered_map<uint32_t, bool> m_hud_metric_enabled;

    std::function<bool(MetricsPage&)> m_on_logging_enable_requested;
    std::function<void(const std::vector<uint32_t>&)> m_on_hud_metrics_changed;

    // Hit rects for checkboxes
    struct CheckRect {
        float x0, y0, x1, y1;
        uint32_t metric_id;    // UINT32_MAX = group toggle
        int      group_idx;
        std::string cluster;
    };
    std::vector<CheckRect> m_check_rects;

    struct ButtonRect {
        float x0, y0, x1, y1;
        int action;
    };
    std::vector<ButtonRect> m_button_rects;

    struct ClusterRect {
        float x0, y0, x1, y1;
        std::string cluster;
    };
    std::vector<ClusterRect> m_cluster_rects;

    static constexpr int ACTION_LOGGING = 1;
    static constexpr int ACTION_FORMAT_JSONL = 2;
    static constexpr int ACTION_FORMAT_CSV = 3;
    static constexpr int ACTION_DISPLAY_ALL = 4;
    static constexpr int ACTION_DEFAULT_DISPLAY = 5;
    static constexpr int ACTION_HUD_ALL = 6;

    void DrawLogGroupSection(float x, float& y, float w);
    void DrawMetricCluster(float x, float& y, float w, const std::string& cluster);
    void DrawCheckRow(float x, float y, float w, bool checked,
                      const std::string& label, const std::string& unit);
    void DrawScrollbar(float x, float y, float w, float h);
    void ClampScroll();
    void ScrollToThumbY(float y);
    void NotifyHudChanged();

    static const char* ClusterLabel(const std::string& id);
};

} // namespace Client
