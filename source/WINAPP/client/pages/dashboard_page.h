#pragma once
#include "../renderer/d2d_context.h"
#include "../renderer/waveform_graph.h"
#include "../config/dashboard_profile.h"
#include "../../shared/shm_layout.h"
#include <vector>
#include <memory>
#include <string>
#include <functional>
#include <unordered_map>

namespace Client {

// Forward
struct ShmView;

struct ClusterHeader {
    std::string  label;
    std::string  cluster_id;
    D2D1_COLOR_F color;
    bool         collapsed = false;
    float        y0 = 0, y1 = 0; // hit-test rects set by Draw()
};

class DashboardPage {
public:
    explicit DashboardPage(D2DContext& ctx);
    ~DashboardPage();

    void SetProfile(DashboardProfile* profile);
    DashboardProfile* Profile() const { return m_profile; }
    void SetSourceLabel(const std::string& label) { m_source_label = label; }
    void ClearMetricValues();
    void SetOnSourceMenuRequested(std::function<void()> cb) { m_on_source_menu_requested = std::move(cb); }

    // Called each data tick (~1Hz): push fresh values into ring buffers
    void PushMetricValue(uint32_t metric_id, float value);

    // Called each render frame (60fps)
    void Draw(float x, float y, float w, float h, float dpi_scale);

    // Mouse events (local coords within the page content area)
    void OnClick(float x, float y);
    void OnMouseMove(float x, float y);
    void OnMouseUp();
    void OnScroll(float delta);
    bool IsEditMode() const { return m_edit_mode; }
    bool IsEditScrollbarDragging() const { return m_edit_scroll_dragging; }
    bool IsScrollbarDragging() const { return m_scroll_dragging; }
    bool ConsumeProfileSaveRequested();

    // Viz type dropdown for a metric (returns true if changed)
    bool ShowVizDropdown(uint32_t metric_id, VizType current, VizType& out_new);

    // Exports the current scroll offset + viz overrides into the profile
    void FlushToProfile();

private:
    D2DContext&       m_ctx;
    DashboardProfile* m_profile = nullptr;
    std::string        m_source_label = "This Device";
    std::function<void()> m_on_source_menu_requested;

    // Per-metric ring of 300 values (mirrors service-side MetricRing)
    struct LocalRing {
        float   values[300]{};
        int     head = 0;
        int     count = 0;
        float   current = 0, min_s = FLT_MAX, max_s = -FLT_MAX;
        void Push(float v);
    };
    std::unordered_map<uint32_t, LocalRing>       m_rings;
    std::unordered_map<uint32_t, WaveformGraph*>  m_waves; // one per line-graph panel

    // Override table: user has changed viz type for this metric
    std::unordered_map<uint32_t, VizType> m_viz_overrides;

    // Dropdown state
    uint32_t m_dropdown_metric_id = UINT32_MAX;
    float    m_dropdown_x = 0, m_dropdown_y = 0;
    bool     m_dropdown_open = false;

    struct PanelHit {
        float x0, y0, x1, y1;
        uint32_t metric_id;
    };
    std::vector<PanelHit> m_panel_hits;

    struct EditHit {
        float x0, y0, x1, y1;
        uint32_t metric_id;
        int action;
    };
    std::vector<EditHit> m_edit_hits;

    // Cluster headers
    std::vector<ClusterHeader> m_clusters;
    std::unordered_map<std::string, bool> m_collapsed; // cluster_id → collapsed

    float m_scroll_y     = 0;
    float m_content_h    = 0;   // total rendered height
    float m_view_h       = 0;   // visible height
    bool  m_scroll_dragging = false;
    float m_scroll_drag_offset = 0.0f;
    float m_scroll_rail_x0 = 0.0f;
    float m_scroll_rail_y0 = 0.0f;
    float m_scroll_rail_x1 = 0.0f;
    float m_scroll_rail_y1 = 0.0f;
    float m_scroll_thumb_x0 = 0.0f;
    float m_scroll_thumb_y0 = 0.0f;
    float m_scroll_thumb_x1 = 0.0f;
    float m_scroll_thumb_y1 = 0.0f;

    // Per-frame layout
    float m_page_x = 0, m_page_y = 0, m_page_w = 0;
    float m_source_x = 0, m_source_y = 0, m_source_w = 0, m_source_h = 0;
    float m_show_all_x = 0, m_show_all_y = 0, m_show_all_w = 84, m_show_all_h = 22;
    float m_edit_x = 0, m_edit_y = 0, m_edit_w = 390, m_edit_h = 0;
    float m_edit_scroll_y = 0;
    float m_edit_content_h = 0;
    bool  m_edit_mode = false;
    bool  m_profile_save_requested = false;
    bool  m_has_edit_backup = false;
    DashboardProfile m_edit_backup;
    bool  m_edit_scroll_dragging = false;
    float m_edit_scroll_drag_offset = 0.0f;
    float m_edit_scroll_rail_x0 = 0.0f;
    float m_edit_scroll_rail_y0 = 0.0f;
    float m_edit_scroll_rail_x1 = 0.0f;
    float m_edit_scroll_rail_y1 = 0.0f;
    float m_edit_scroll_thumb_x0 = 0.0f;
    float m_edit_scroll_thumb_y0 = 0.0f;
    float m_edit_scroll_thumb_x1 = 0.0f;
    float m_edit_scroll_thumb_y1 = 0.0f;

    static constexpr int EDIT_TOGGLE = 1;
    static constexpr int EDIT_SAVE_CLOSE = 2;
    static constexpr int EDIT_RESET_DEFAULT = 3;
    static constexpr int EDIT_SHOW_ALL = 4;
    static constexpr int EDIT_TOGGLE_VISIBLE = 5;
    static constexpr int EDIT_CYCLE_VIZ = 6;
    static constexpr int EDIT_SIZE_WIDER = 7;
    static constexpr int EDIT_SIZE_NARROWER = 8;
    static constexpr int EDIT_SIZE_TALLER = 9;
    static constexpr int EDIT_SIZE_SHORTER = 10;
    static constexpr int EDIT_CANCEL = 11;
    static constexpr int EDIT_SAVE_DEFAULT = 12;

    static constexpr float CLUSTER_H      = 32.0f;
    static constexpr float PANEL_ROW_H    = 120.0f;
    static constexpr float PANEL_GAP      = 6.0f;
    static constexpr float COLS           = 12.0f;

    // Draw helpers
    void DrawClusterHeader(float x, float y, float w, ClusterHeader& hdr);
    void DrawPanel(const MetricPanel& mp, float x, float y, float pw, float ph);
    void DrawLineGraph(const MetricPanel& mp, const LocalRing& ring, float x, float y, float w, float h);
    void DrawArcGauge(const MetricPanel& mp, float val, float x, float y, float w, float h);
    void DrawBarGauge(const MetricPanel& mp, float val, float x, float y, float w, float h);
    void DrawNumeral(const MetricPanel& mp, float val, float x, float y, float w, float h);
    void DrawNumeralTrend(const MetricPanel& mp, const LocalRing& ring, float x, float y, float w, float h);
    void DrawHeatMap(const MetricPanel& mp, float x, float y, float w, float h);
    void DrawDualLine(const MetricPanel& mp, const LocalRing& ring, float x, float y, float w, float h);
    void DrawLed(const MetricPanel& mp, float val, float x, float y, float w, float h);
    void DrawTopControls(float x, float y, float w);
    void DrawEditDrawer(float x, float y, float w, float h);
    void DrawSmallButton(float x, float y, float w, const wchar_t* label, int action, uint32_t metric_id = UINT32_MAX);
    void DrawScrollbar(float x, float y, float w, float h);
    void DrawEditScrollbar(float x, float y, float w, float h);
    void ClampScroll();
    void ClampEditScroll();
    void BeginEditSession();
    void CommitEditSession();
    void CancelEditSession();
    void ScrollToThumbY(float y);
    void ScrollEditToThumbY(float y);

    void EnsureWaveform(uint32_t metric_id, int w, int h);
    const LocalRing* GetRing(uint32_t id) const;
    float GetCurrent(uint32_t id) const;

    void RebuildClusters();
};

} // namespace Client
