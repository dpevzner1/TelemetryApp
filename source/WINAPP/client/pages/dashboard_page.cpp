#include "dashboard_page.h"
#include "../renderer/waveform_graph.h"
#include <d2d1_1helper.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <iomanip>

namespace Client {

// ── LocalRing ─────────────────────────────────────────────────────────────────

void DashboardPage::LocalRing::Push(float v) {
    values[head] = v;
    head = (head + 1) % 300;
    if (count < 300) ++count;
    current = v;
    if (v < min_s) min_s = v;
    if (v > max_s) max_s = v;
}

// ── Constructor ───────────────────────────────────────────────────────────────

DashboardPage::DashboardPage(D2DContext& ctx) : m_ctx(ctx) {}

DashboardPage::~DashboardPage() {
    for (auto& [id, w] : m_waves) delete w;
}

void DashboardPage::SetProfile(DashboardProfile* profile) {
    m_profile = profile;
    RebuildClusters();
}

// ── Data ──────────────────────────────────────────────────────────────────────

void DashboardPage::PushMetricValue(uint32_t metric_id, float value) {
    m_rings[metric_id].Push(value);
    auto it = m_waves.find(metric_id);
    if (it != m_waves.end()) it->second->Push(value);
}

void DashboardPage::ClearMetricValues() {
    m_rings.clear();
    for (auto& kv : m_waves) {
        delete kv.second;
    }
    m_waves.clear();
}

const DashboardPage::LocalRing* DashboardPage::GetRing(uint32_t id) const {
    auto it = m_rings.find(id);
    return it != m_rings.end() ? &it->second : nullptr;
}

float DashboardPage::GetCurrent(uint32_t id) const {
    auto r = GetRing(id);
    return r ? r->current : 0.0f;
}

// ── Cluster list ──────────────────────────────────────────────────────────────

struct CInfo { const char* id; const char* label; D2D1_COLOR_F color; };
static const CInfo kClusters[] = {
    {"cpu",     "CPU",             {0.30f, 0.80f, 0.40f, 1}},
    {"cpu_cores","CPU Cores",      {0.25f, 0.70f, 0.35f, 1}},
    {"memory",  "Memory",          {0.95f, 0.65f, 0.10f, 1}},
    {"gpu",     "GPU",             {0.25f, 0.60f, 1.00f, 1}},
    {"disk",    "Storage",         {0.85f, 0.40f, 0.90f, 1}},
    {"network", "Network",         {0.40f, 0.85f, 0.90f, 1}},
    {"temp",    "Thermals",        {1.00f, 0.40f, 0.20f, 1}},
    {"process", "Process Watcher", {0.80f, 0.75f, 0.30f, 1}},
    {"self",    "Self-Monitoring", {0.55f, 0.55f, 0.58f, 1}},
};

void DashboardPage::RebuildClusters() {
    m_clusters.clear();
    if (!m_profile) return;
    // Add each cluster that has at least one visible panel
    for (const auto& ci : kClusters) {
        bool has = false;
        for (const auto& mp : m_profile->panels) {
            if (mp.cluster == ci.id && mp.visible) { has = true; break; }
        }
        if (!has) continue;
        ClusterHeader hdr;
        hdr.cluster_id = ci.id;
        hdr.label      = ci.label;
        hdr.color      = ci.color;
        hdr.collapsed  = m_collapsed.count(ci.id) ? m_collapsed.at(ci.id) : false;
        m_clusters.push_back(hdr);
    }
}

// ── Draw ─────────────────────────────────────────────────────────────────────

void DashboardPage::Draw(float x, float y, float w, float h, float /*dpi*/) {
    m_page_x = x; m_page_y = y; m_page_w = w; m_view_h = h;
    m_panel_hits.clear();
    m_edit_hits.clear();
    auto* dc = m_ctx.DC();

    // Clip to page rect
    D2D1_RECT_F clip = {x, y, x + w, y + h};
    dc->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);

    DrawTopControls(x, y, w);

    float content_w = m_edit_mode ? std::max(560.0f, w - m_edit_w - 12.0f) : w;
    float cy = y + 34.0f - m_scroll_y;  // current draw Y (panel Y coordinate)

    if (!m_profile) {
        m_ctx.DrawText(L"No profile loaded", {x+20, y+40, x+w, y+80},
                       {0.5f,0.5f,0.5f,1}, 14.0f);
        dc->PopAxisAlignedClip();
        return;
    }

    // Build a per-cluster list of visible panels
    for (auto& hdr : m_clusters) {
        // Cluster header bar
        hdr.y0 = cy;
        DrawClusterHeader(x + 4.0f, cy, content_w - 8.0f, hdr);
        cy += CLUSTER_H + 4.0f;
        hdr.y1 = cy;

        if (hdr.collapsed) continue;

        // Gather panels for this cluster sorted by grid row then col
        std::vector<const MetricPanel*> plist;
        for (const auto& mp : m_profile->panels)
            if (mp.cluster == hdr.cluster_id && mp.visible)
                plist.push_back(&mp);
        std::sort(plist.begin(), plist.end(), [](const MetricPanel* a, const MetricPanel* b){
            if (a->grid_row != b->grid_row) return a->grid_row < b->grid_row;
            return a->grid_col < b->grid_col;
        });

        // Lay out visible panels in a packed 12-column grid. Hidden panels should
        // not leave visual holes; grid_col/grid_row remain saved preferences, but
        // the live dashboard packs remaining cards left for readability.
        float cell_w = (content_w - 8.0f - PANEL_GAP * 11.0f) / COLS;
        int packed_col = 0;
        float row_y = cy;
        float row_h = 0.0f;
        for (const auto* mp : plist) {
            int span = std::clamp(mp->grid_col_span, 1, 12);
            if (packed_col > 0 && packed_col + span > 12) {
                cy += row_h + PANEL_GAP;
                row_y = cy;
                packed_col = 0;
                row_h = 0.0f;
            }
            float px = x + 4.0f + packed_col * (cell_w + PANEL_GAP);
            float pw  = cell_w * span + PANEL_GAP * (span - 1);
            float ph  = PANEL_ROW_H * mp->grid_row_span + PANEL_GAP * (mp->grid_row_span - 1);
            float ry  = row_y;
            if (ry + ph > y && ry < y + h) { // cull off-screen
                m_panel_hits.push_back({px, ry, px + pw, ry + ph, mp->metric_id});
                DrawPanel(*mp, px, ry, pw, ph);
            }
            packed_col += span;
            row_h = std::max(row_h, ph);
        }
        if (!plist.empty()) cy += row_h + PANEL_GAP;
        cy += 8.0f; // cluster bottom padding
    }

    m_content_h = cy - (y - m_scroll_y) + m_scroll_y;
    if (m_edit_mode) DrawEditDrawer(x + w - m_edit_w, y, m_edit_w, h);
    dc->PopAxisAlignedClip();
}

void DashboardPage::DrawSmallButton(float x, float y, float w, const wchar_t* label,
                                    int action, uint32_t metric_id) {
    D2D1_RECT_F r = {x, y, x + w, y + 22.0f};
    auto* dc = m_ctx.DC();
    dc->FillRoundedRectangle(D2D1::RoundedRect(r, 4, 4),
        m_ctx.BrushSolid({0.16f, 0.16f, 0.19f, 0.96f}));
    dc->DrawRoundedRectangle(D2D1::RoundedRect(r, 4, 4),
        m_ctx.BrushSolid({0.95f, 0.24f, 0.24f, 1.0f}), 1.0f);

    float pad_x = (w <= 28.0f) ? 2.0f : ((w <= 52.0f) ? 4.0f : 7.0f);
    float font_pt = (w <= 28.0f) ? 8.0f : 8.6f;
    D2D1_RECT_F text_r = {
        r.left + pad_x,
        r.top + 1.0f,
        r.right - pad_x,
        r.bottom - 1.0f
    };
    IDWriteTextFormat* fmt = m_ctx.MakeTextFormat(font_pt, true, true);
    if (fmt) {
        fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        dc->DrawTextW(label, static_cast<UINT32>(wcslen(label)), fmt, text_r,
                      m_ctx.BrushSolid({0.95f, 0.82f, 0.82f, 1.0f}),
                      D2D1_DRAW_TEXT_OPTIONS_CLIP);
        fmt->Release();
    }
    m_edit_hits.push_back({r.left, r.top, r.right, r.bottom, metric_id, action});
}

void DashboardPage::DrawTopControls(float x, float y, float w) {
    std::string label = "Dashboard View: " + (m_source_label.empty() ? std::string("This Device") : m_source_label);
    std::wstring ws(label.begin(), label.end());
    m_source_x = x + 14.0f;
    m_source_y = y + 5.0f;
    m_source_w = std::max(260.0f, std::min(520.0f, w - 880.0f));
    m_source_h = 24.0f;
    D2D1_RECT_F source_r = {m_source_x, m_source_y, m_source_x + m_source_w, m_source_y + m_source_h};
    m_ctx.DC()->FillRoundedRectangle(D2D1::RoundedRect(source_r, 4, 4),
        m_ctx.BrushSolid({0.12f, 0.15f, 0.19f, 0.96f}));
    m_ctx.DC()->DrawRoundedRectangle(D2D1::RoundedRect(source_r, 4, 4),
        m_ctx.BrushSolid({0.25f, 0.60f, 1.0f, 0.9f}), 0.8f);
    m_ctx.DrawText(ws.c_str(), {source_r.left + 8.0f, source_r.top + 4.0f, source_r.right - 24.0f, source_r.bottom},
                   {0.70f, 0.82f, 1.0f, 1.0f}, 11.0f, true);
    m_ctx.DrawText(L"v", {source_r.right - 18.0f, source_r.top + 3.0f, source_r.right - 4.0f, source_r.bottom},
                   {0.70f, 0.82f, 1.0f, 1.0f}, 10.0f, true);

    float bx = x + w - (m_edit_mode ? 612.0f : 232.0f);
    DrawSmallButton(bx, y + 6.0f, 104.0f, m_edit_mode ? L"Save & Close" : L"Edit Dashboard",
                    m_edit_mode ? EDIT_SAVE_CLOSE : EDIT_TOGGLE);
    if (!m_edit_mode) {
        DrawSmallButton(bx + 112.0f, y + 6.0f, 84.0f, L"Show All", EDIT_SHOW_ALL);
        return;
    }
    DrawSmallButton(bx + 112.0f, y + 6.0f, 72.0f, L"Cancel", EDIT_CANCEL);
    DrawSmallButton(bx + 192.0f, y + 6.0f, 84.0f, L"Show All", EDIT_SHOW_ALL);
    if (m_edit_mode) {
        DrawSmallButton(bx + 284.0f, y + 6.0f, 116.0f, L"Reset Default", EDIT_RESET_DEFAULT);
        DrawSmallButton(bx + 408.0f, y + 6.0f, 132.0f, L"Save Default", EDIT_SAVE_DEFAULT);
    }
}

void DashboardPage::DrawEditDrawer(float x, float y, float w, float h) {
    m_edit_x = x; m_edit_y = y; m_edit_w = w; m_edit_h = h;
    auto* dc = m_ctx.DC();
    D2D1_RECT_F bg = {x, y, x + w, y + h};
    dc->FillRectangle(bg, m_ctx.BrushSolid({0.08f, 0.08f, 0.10f, 0.98f}));
    dc->DrawLine({x, y}, {x, y + h}, m_ctx.BrushSolid({0.28f,0.28f,0.34f,1}), 1.0f);

    m_ctx.DrawText(L"Dashboard Editor", {x + 16, y + 14, x + w - 250.0f, y + 40},
                   {0.92f,0.92f,0.95f,1}, 16.0f, true);
    DrawSmallButton(x + w - 236.0f, y + 12.0f, 74.0f, L"Save", EDIT_SAVE_CLOSE);
    DrawSmallButton(x + w - 156.0f, y + 12.0f, 68.0f, L"Cancel", EDIT_CANCEL);
    DrawSmallButton(x + w - 82.0f, y + 12.0f, 66.0f, L"Default", EDIT_SAVE_DEFAULT);
    m_ctx.DrawText(L"Visible | Visualization | Size", {x + 16, y + 42, x + w - 252.0f, y + 60},
                   {0.52f,0.52f,0.58f,1}, 9.0f);

    float cy = y + 70.0f - m_edit_scroll_y;
    if (!m_profile) return;

    for (auto& mp : m_profile->panels) {
        if (cy > y + h) break;
        if (cy + 60.0f >= y + 62.0f) {
            D2D1_RECT_F row = {x + 12, cy, x + w - 18, cy + 54};
            dc->FillRoundedRectangle(D2D1::RoundedRect(row, 5, 5),
                m_ctx.BrushSolid(mp.visible ? D2D1_COLOR_F{0.13f,0.13f,0.16f,1}
                                            : D2D1_COLOR_F{0.09f,0.09f,0.11f,1}));
            dc->DrawRoundedRectangle(D2D1::RoundedRect(row, 5, 5),
                m_ctx.BrushSolid({0.22f,0.22f,0.27f,1}), 0.7f);

            std::wstring label(mp.label.begin(), mp.label.end());
            m_ctx.DrawText(label.c_str(), {x + 22, cy + 7, x + 178, cy + 26},
                           mp.visible ? D2D1_COLOR_F{0.88f,0.88f,0.90f,1}
                                      : D2D1_COLOR_F{0.44f,0.44f,0.48f,1}, 10.5f, true);

            const char* vn = VizTypeName(mp.viz_type);
            std::wstring viz(vn, vn + strlen(vn));
            m_ctx.DrawText(viz.c_str(), {x + 22, cy + 28, x + 168, cy + 46},
                           {0.54f,0.54f,0.60f,1}, 9.0f);

            DrawSmallButton(x + 184, cy + 8, 48, mp.visible ? L"Hide" : L"Show",
                            EDIT_TOGGLE_VISIBLE, mp.metric_id);
            DrawSmallButton(x + 238, cy + 8, 48, L"Viz", EDIT_CYCLE_VIZ, mp.metric_id);
            DrawSmallButton(x + 292, cy + 8, 24, L"-W", EDIT_SIZE_NARROWER, mp.metric_id);
            DrawSmallButton(x + 320, cy + 8, 24, L"+W", EDIT_SIZE_WIDER, mp.metric_id);
            DrawSmallButton(x + 292, cy + 31, 24, L"-H", EDIT_SIZE_SHORTER, mp.metric_id);
            DrawSmallButton(x + 320, cy + 31, 24, L"+H", EDIT_SIZE_TALLER, mp.metric_id);

            char sz[32]{};
            snprintf(sz, sizeof(sz), "%dx%d", mp.grid_col_span, mp.grid_row_span);
            std::wstring wsz(sz, sz + strlen(sz));
            m_ctx.DrawText(wsz.c_str(), {x + 350, cy + 20, x + w - 24, cy + 40},
                           {0.60f,0.60f,0.65f,1}, 9.0f);
        }
        cy += 60.0f;
    }
    m_edit_content_h = (float)m_profile->panels.size() * 60.0f + 82.0f;
    ClampEditScroll();
    DrawEditScrollbar(x, y + 62.0f, w, h - 68.0f);
}

void DashboardPage::DrawClusterHeader(float x, float y, float w, ClusterHeader& hdr) {
    D2D1_RECT_F bg = {x, y, x + w, y + CLUSTER_H};
    D2D1_COLOR_F bg_col = {hdr.color.r * 0.2f, hdr.color.g * 0.2f, hdr.color.b * 0.2f, 1.0f};
    m_ctx.DC()->FillRoundedRectangle(D2D1::RoundedRect(bg, 4, 4), m_ctx.BrushSolid(bg_col));
    D2D1_COLOR_F accent = hdr.color;
    m_ctx.DC()->FillRoundedRectangle(D2D1::RoundedRect({x, y, x + 4, y + CLUSTER_H}, 2, 2),
                                      m_ctx.BrushSolid(accent));
    std::string tri = hdr.collapsed ? " ▶ " : " ▼ ";
    std::wstring ws = std::wstring(tri.begin(), tri.end())
                    + std::wstring(hdr.label.begin(), hdr.label.end());
    m_ctx.DrawText(ws.c_str(), {x + 10, y + 6, x + w - 4, y + CLUSTER_H - 4},
                   hdr.color, 12.0f, true);
}

void DashboardPage::DrawPanel(const MetricPanel& mp, float x, float y, float pw, float ph) {
    // Panel background + border
    D2D1_RECT_F bg = {x, y, x + pw, y + ph};
    m_ctx.DC()->FillRoundedRectangle(D2D1::RoundedRect(bg, 6, 6),
        m_ctx.BrushSolid({0.12f, 0.12f, 0.15f, 1.0f}));
    m_ctx.DC()->DrawRoundedRectangle(D2D1::RoundedRect(bg, 6, 6),
        m_ctx.BrushSolid({0.20f, 0.20f, 0.24f, 1.0f}), 0.5f);

    // Determine effective viz type (check override table)
    VizType viz = mp.viz_type;
    auto ov = m_viz_overrides.find(mp.metric_id);
    if (ov != m_viz_overrides.end()) viz = ov->second;

    float val = GetCurrent(mp.metric_id);
    const LocalRing* ring = GetRing(mp.metric_id);

    switch (viz) {
    case VizType::LineGraph:
        if (ring) DrawLineGraph(mp, *ring, x + 4, y + 4, pw - 8, ph - 4);
        break;
    case VizType::ArcGauge:
        DrawArcGauge(mp, val, x, y, pw, ph);
        break;
    case VizType::BarGauge:
        DrawBarGauge(mp, val, x, y, pw, ph);
        break;
    case VizType::Numeral:
        DrawNumeral(mp, val, x, y, pw, ph);
        break;
    case VizType::NumeralTrend:
        DrawNumeralTrend(mp, ring ? *ring : LocalRing{}, x, y, pw, ph);
        break;
    case VizType::HeatMap:
        DrawHeatMap(mp, x, y, pw, ph);
        break;
    case VizType::DualLine:
        if (ring) DrawDualLine(mp, *ring, x + 4, y + 4, pw - 8, ph - 4);
        break;
    case VizType::LedIndicator:
        DrawLed(mp, val, x, y, pw, ph);
        break;
    default: break;
    }

    // Label at bottom-left
    std::wstring wlabel(mp.label.begin(), mp.label.end());
    m_ctx.DrawText(wlabel.c_str(),
        {x + 6, y + ph - 18, x + pw - 24, y + ph - 2},
        {0.55f, 0.55f, 0.58f, 1.0f}, 10.0f);

    // Viz-type dropdown trigger ▼ at top-right
    m_ctx.DrawText(L"×",
        {x + pw - 36, y + 3, x + pw - 20, y + 18},
        {0.55f, 0.32f, 0.32f, 1.0f}, 10.0f);
    m_ctx.DrawText(L"▾",
        {x + pw - 18, y + 3, x + pw - 2, y + 18},
        {0.95f, 0.24f, 0.24f, 1.0f}, 10.0f);

    const char* vname = VizTypeName(viz);
    std::wstring wv(vname, vname + strlen(vname));
    m_ctx.DrawText(wv.c_str(),
        {x + pw - 100, y + 3, x + pw - 20, y + 18},
        {0.42f, 0.42f, 0.46f, 1.0f}, 8.5f);
}

// ── Arc Gauge ─────────────────────────────────────────────────────────────────

void DashboardPage::DrawArcGauge(const MetricPanel& mp, float val,
                                  float x, float y, float w, float h) {
    auto* dc = m_ctx.DC();
    float cx  = x + w / 2.0f;
    float cy2 = y + h * 0.58f;
    float r   = std::min(w, h) * 0.38f;
    float frac = (mp.y_max > mp.y_min) ? (val - mp.y_min) / (mp.y_max - mp.y_min) : 0;
    frac = std::clamp(frac, 0.0f, 1.0f);

    const float kStart = 3.14159f * 0.75f;   // 135°
    const float kSweep = 3.14159f * 1.5f;    // 270°

    // Track (background arc) — drawn as polyline segments
    int segs = 90;
    for (int i = 0; i < segs; ++i) {
        float a0 = kStart + kSweep * i / segs;
        float a1 = kStart + kSweep * (i + 1) / segs;
        bool is_filled = (float)i / segs < frac;
        D2D1_COLOR_F col = {0.20f, 0.20f, 0.22f, 1.0f};
        // Parse hex color for filled segment
        if (is_filled && mp.color.size() == 7 && mp.color[0] == '#') {
            auto hex = [](char c) -> float {
                if (c >= '0' && c <= '9') return (c-'0')/255.0f*16;
                if (c >= 'a' && c <= 'f') return (c-'a'+10)/255.0f*16;
                if (c >= 'A' && c <= 'F') return (c-'A'+10)/255.0f*16;
                return 0;
            };
            col.r = hex(mp.color[1]) + hex(mp.color[2])/16.0f;
            col.g = hex(mp.color[3]) + hex(mp.color[4])/16.0f;
            col.b = hex(mp.color[5]) + hex(mp.color[6])/16.0f;
            col.a = 1.0f;
        }
        dc->DrawLine(
            {cx + r * cosf(a0), cy2 + r * sinf(a0)},
            {cx + r * cosf(a1), cy2 + r * sinf(a1)},
            m_ctx.BrushSolid(col), 6.0f,
            nullptr);
    }

    // Value text
    char buf[32]{};
    snprintf(buf, sizeof(buf), "%.1f", val);
    std::wstring ws(buf, buf + strlen(buf));
    m_ctx.DrawText(ws.c_str(), {x, cy2 - 20, x + w, cy2 + 10},
                   {0.90f,0.90f,0.92f,1.0f}, 22.0f, true);

    // Unit
    std::wstring wunit(mp.unit.begin(), mp.unit.end());
    m_ctx.DrawText(wunit.c_str(), {x, cy2 + 12, x + w, cy2 + 26},
                   {0.50f,0.50f,0.54f,1.0f}, 10.0f, false);
}

// ── Bar Gauge ─────────────────────────────────────────────────────────────────

void DashboardPage::DrawBarGauge(const MetricPanel& mp, float val,
                                  float x, float y, float w, float h) {
    auto* dc = m_ctx.DC();
    float frac = (mp.y_max > mp.y_min) ? (val - mp.y_min) / (mp.y_max - mp.y_min) : 0;
    frac = std::clamp(frac, 0.0f, 1.0f);

    // Track
    D2D1_RECT_F track = {x + 8, y + h * 0.50f, x + w - 8, y + h * 0.50f + 10};
    dc->FillRoundedRectangle(D2D1::RoundedRect(track, 4, 4),
        m_ctx.BrushSolid({0.20f, 0.20f, 0.22f, 1.0f}));

    // Fill
    D2D1_RECT_F fill = {track.left, track.top, track.left + (track.right - track.left) * frac, track.bottom};
    if (fill.right > fill.left)
        dc->FillRoundedRectangle(D2D1::RoundedRect(fill, 4, 4),
            m_ctx.BrushSolid({0.25f, 0.63f, 1.0f, 1.0f}));

    // Value
    char buf[32]{};
    snprintf(buf, sizeof(buf), "%.1f %s", val, mp.unit.c_str());
    std::wstring ws(buf, buf + strlen(buf));
    m_ctx.DrawText(ws.c_str(), {x + 8, y + 10, x + w - 8, y + h * 0.46f},
                   {0.90f,0.90f,0.92f,1.0f}, 18.0f, true);
}

// ── Numeral ───────────────────────────────────────────────────────────────────

void DashboardPage::DrawNumeral(const MetricPanel& mp, float val,
                                 float x, float y, float w, float h) {
    char buf[32]{};
    if (mp.unit == "MHz" || mp.unit == "MB" || mp.unit == "GB")
        snprintf(buf, sizeof(buf), "%.0f", val);
    else
        snprintf(buf, sizeof(buf), "%.1f", val);
    std::wstring ws(buf, buf + strlen(buf));
    m_ctx.DrawText(ws.c_str(), {x, y + 10, x + w, y + h - 20},
                   {0.90f,0.90f,0.92f,1.0f}, 26.0f, true);
    std::wstring wu(mp.unit.begin(), mp.unit.end());
    m_ctx.DrawText(wu.c_str(), {x, y + h - 28, x + w, y + h - 12},
                   {0.45f,0.45f,0.48f,1.0f}, 10.0f, false);
}

// ── Numeral + Trend (sparkline) ────────────────────────────────────────────────

void DashboardPage::DrawNumeralTrend(const MetricPanel& mp, const LocalRing& ring,
                                      float x, float y, float w, float h) {
    DrawNumeral(mp, ring.current, x, y, w, h * 0.60f);

    // Mini sparkline in lower 40%
    int n = std::min(ring.count, 60);
    if (n < 2) return;
    float sy = y + h * 0.62f, sh = h * 0.30f;
    float dx = (w - 16.0f) / (n - 1);
    float rng = ring.max_s - ring.min_s;
    if (rng < 0.001f) rng = 1.0f;
    auto* dc = m_ctx.DC();
    for (int i = 1; i < n; ++i) {
        int ia = (ring.head - n + i - 1 + 300) % 300;
        int ib = (ring.head - n + i     + 300) % 300;
        float va = (ring.values[ia] - ring.min_s) / rng;
        float vb = (ring.values[ib] - ring.min_s) / rng;
        dc->DrawLine(
            {x + 8 + dx * (i-1), sy + sh * (1.0f - va)},
            {x + 8 + dx * i,     sy + sh * (1.0f - vb)},
            m_ctx.BrushSolid({0.25f,0.63f,1.0f,0.8f}), 1.5f);
    }
}

// ── Heat Map (per-core CPU) ───────────────────────────────────────────────────

void DashboardPage::DrawHeatMap(const MetricPanel& mp, float x, float y, float w, float h) {
    // Metric IDs for per-core usage are consecutive starting at metric_id
    // (cpu_core_metric(core, CpuCoreOff::PCT) pattern from metric_ids.h)
    // We draw a grid of up to 32 cores, 16 per row
    const int MAX_CORES = 32;
    const int COLS_HEAT = 16;
    const int ROWS_HEAT = MAX_CORES / COLS_HEAT;
    float cw = (w - 8.0f - 4.0f * (COLS_HEAT - 1)) / COLS_HEAT;
    float ch = (h - 24.0f - 4.0f * (ROWS_HEAT - 1)) / ROWS_HEAT;
    auto* dc = m_ctx.DC();
    for (int c = 0; c < MAX_CORES; ++c) {
        uint32_t id = mp.metric_id + c;  // consecutive IDs
        float val = GetCurrent(id);
        float frac = val / 100.0f;
        // Green → yellow → red gradient
        D2D1_COLOR_F col;
        if (frac < 0.5f) {
            col = {frac * 2.0f * 0.9f, 0.75f, frac * 2.0f * 0.1f, 1.0f};
        } else {
            float t = (frac - 0.5f) * 2.0f;
            col = {0.9f, 0.75f * (1.0f - t), 0.0f, 1.0f};
        }
        int row = c / COLS_HEAT, col_i = c % COLS_HEAT;
        float cx2 = x + 4 + col_i * (cw + 4);
        float cy2 = y + 20 + row * (ch + 4);
        D2D1_RECT_F r = {cx2, cy2, cx2 + cw, cy2 + ch};
        dc->FillRoundedRectangle(D2D1::RoundedRect(r, 3, 3), m_ctx.BrushSolid(col));
        // Core number
        char buf[4]{};
        snprintf(buf, sizeof(buf), "%d", c);
        std::wstring ws(buf, buf + strlen(buf));
        m_ctx.DrawText(ws.c_str(), r, {0,0,0,0.7f}, 8.0f);
    }
}

// ── Line Graph ────────────────────────────────────────────────────────────────

void DashboardPage::DrawLineGraph(const MetricPanel& mp, const LocalRing& ring,
                                   float x, float y, float w, float h) {
    int n = std::min(ring.count, mp.history_samples);
    if (n < 2) return;
    auto* dc = m_ctx.DC();
    float dx = w / (n - 1);
    float yrange = mp.y_max - mp.y_min;
    if (yrange < 0.001f) yrange = 1.0f;

    // Grid line at 50%
    float gy = y + h * 0.5f;
    dc->DrawLine({x, gy}, {x + w, gy}, m_ctx.BrushSolid({0.20f,0.20f,0.24f,0.5f}), 0.5f);

    for (int i = 1; i < n; ++i) {
        int ia = (ring.head - n + i - 1 + 300) % 300;
        int ib = (ring.head - n + i     + 300) % 300;
        float va = std::clamp((ring.values[ia] - mp.y_min) / yrange, 0.0f, 1.0f);
        float vb = std::clamp((ring.values[ib] - mp.y_min) / yrange, 0.0f, 1.0f);
        dc->DrawLine(
            {x + dx * (i-1), y + h * (1.0f - va)},
            {x + dx * i,     y + h * (1.0f - vb)},
            m_ctx.BrushSolid({0.25f, 0.63f, 1.0f, 0.9f}), 1.5f);
    }

    // Current value label
    char buf[24]{};
    snprintf(buf, sizeof(buf), "%.1f", ring.current);
    std::wstring ws(buf, buf + strlen(buf));
    m_ctx.DrawText(ws.c_str(), {x, y, x + w, y + 20},
                   {0.85f,0.85f,0.88f,1.0f}, 11.0f, true);
}

// ── Dual Line ─────────────────────────────────────────────────────────────────

void DashboardPage::DrawDualLine(const MetricPanel& mp, const LocalRing& ring,
                                  float x, float y, float w, float h) {
    // ring = primary (read); secondary (write) at metric_id + 1
    DrawLineGraph(mp, ring, x, y, w, h);
    // Draw secondary ring in orange overlay if it exists
    const LocalRing* ring2 = GetRing(mp.metric_id + 1);
    if (!ring2 || ring2->count < 2) return;
    auto* dc = m_ctx.DC();
    int n = std::min(ring2->count, mp.history_samples);
    float dx = w / (n - 1);
    float yrange = mp.y_max - mp.y_min;
    if (yrange < 0.001f) yrange = 1.0f;
    for (int i = 1; i < n; ++i) {
        int ia = (ring2->head - n + i - 1 + 300) % 300;
        int ib = (ring2->head - n + i     + 300) % 300;
        float va = std::clamp((ring2->values[ia] - mp.y_min) / yrange, 0.0f, 1.0f);
        float vb = std::clamp((ring2->values[ib] - mp.y_min) / yrange, 0.0f, 1.0f);
        dc->DrawLine(
            {x + dx * (i-1), y + h * (1.0f - va)},
            {x + dx * i,     y + h * (1.0f - vb)},
            m_ctx.BrushSolid({0.95f, 0.55f, 0.10f, 0.9f}), 1.5f);
    }
}

// ── LED Indicator ─────────────────────────────────────────────────────────────

void DashboardPage::DrawLed(const MetricPanel& mp, float val,
                             float x, float y, float w, float h) {
    bool on = val > 0.5f;
    D2D1_COLOR_F col = on ? D2D1_COLOR_F{0.9f,0.2f,0.2f,1} : D2D1_COLOR_F{0.2f,0.7f,0.3f,1};
    float cx2 = x + w / 2.0f;
    float cy2 = y + h / 2.0f - 4;
    D2D1_ELLIPSE dot = {{cx2, cy2}, 16.0f, 16.0f};
    m_ctx.DC()->FillEllipse(dot, m_ctx.BrushSolid(col));
    m_ctx.DrawText(on ? L"ACTIVE" : L"OK",
                   {x, cy2 + 20, x + w, cy2 + 36},
                   col, 11.0f, true);
}

// ── Click / Scroll ────────────────────────────────────────────────────────────

void DashboardPage::OnClick(float x, float y) {
    if (!m_edit_mode &&
        x >= m_source_x && x <= m_source_x + m_source_w &&
        y >= m_source_y && y <= m_source_y + m_source_h) {
        if (m_on_source_menu_requested) m_on_source_menu_requested();
        return;
    }

    if (m_edit_mode &&
        m_edit_content_h > std::max(1.0f, m_edit_h - 68.0f) + 1.0f &&
        x >= m_edit_scroll_rail_x0 && x <= m_edit_scroll_rail_x1 &&
        y >= m_edit_scroll_rail_y0 && y <= m_edit_scroll_rail_y1) {
        if (y >= m_edit_scroll_thumb_y0 && y <= m_edit_scroll_thumb_y1) {
            m_edit_scroll_dragging = true;
            m_edit_scroll_drag_offset = y - m_edit_scroll_thumb_y0;
        } else {
            ScrollEditToThumbY(y);
        }
        return;
    }

    for (const auto& eh : m_edit_hits) {
        if (x < eh.x0 || x > eh.x1 || y < eh.y0 || y > eh.y1) continue;
        if (eh.action == EDIT_TOGGLE) { BeginEditSession(); return; }
        if (eh.action == EDIT_SAVE_CLOSE) { CommitEditSession(); return; }
        if (eh.action == EDIT_CANCEL) { CancelEditSession(); return; }
        if (eh.action == EDIT_SHOW_ALL && m_profile) {
            for (auto& mp : m_profile->panels) mp.visible = true;
            RebuildClusters();
            return;
        }
        if (eh.action == EDIT_RESET_DEFAULT && m_profile) {
            std::string path = m_profile->FilePath();
            *m_profile = DashboardProfile::MakeDefault();
            m_profile->SetFilePath(path);
            m_viz_overrides.clear();
            m_collapsed.clear();
            RebuildClusters();
            return;
        }
        if (eh.action == EDIT_SAVE_DEFAULT) {
            CommitEditSession();
            return;
        }
        if (!m_profile || eh.metric_id == UINT32_MAX) return;
        for (auto& mp : m_profile->panels) {
            if (mp.metric_id != eh.metric_id) continue;
            switch (eh.action) {
            case EDIT_TOGGLE_VISIBLE:
                mp.visible = !mp.visible;
                RebuildClusters();
                break;
            case EDIT_CYCLE_VIZ: {
                auto allowed = AllowedVizTypes(mp.cluster, mp.unit);
                if (allowed.empty()) allowed = {VizType::LineGraph, VizType::Numeral};
                auto it = std::find(allowed.begin(), allowed.end(), mp.viz_type);
                size_t next = (it == allowed.end()) ? 0
                    : ((size_t)std::distance(allowed.begin(), it) + 1) % allowed.size();
                mp.viz_type = allowed[next];
                m_viz_overrides[mp.metric_id] = mp.viz_type;
                break;
            }
            case EDIT_SIZE_WIDER:
                mp.grid_col_span = std::min(12, mp.grid_col_span + 1);
                break;
            case EDIT_SIZE_NARROWER:
                mp.grid_col_span = std::max(1, mp.grid_col_span - 1);
                break;
            case EDIT_SIZE_TALLER:
                mp.grid_row_span = std::min(3, mp.grid_row_span + 1);
                break;
            case EDIT_SIZE_SHORTER:
                mp.grid_row_span = std::max(1, mp.grid_row_span - 1);
                break;
            }
            return;
        }
        return;
    }

    // Map to content coords
    float cy = y + m_scroll_y;

    if (x >= m_show_all_x && x <= m_show_all_x + m_show_all_w &&
        y >= m_show_all_y && y <= m_show_all_y + m_show_all_h && m_profile) {
        for (auto& mp : m_profile->panels) mp.visible = true;
        RebuildClusters();
        return;
    }

    for (const auto& hit : m_panel_hits) {
        if (x >= hit.x0 && x <= hit.x1 && cy >= hit.y0 && cy <= hit.y1) {
            bool in_hide = x >= hit.x1 - 42.0f && x <= hit.x1 - 20.0f &&
                           cy >= hit.y0 && cy <= hit.y0 + 24.0f;
            bool in_dropdown = x >= hit.x1 - 110.0f && x <= hit.x1 &&
                               cy >= hit.y0 && cy <= hit.y0 + 24.0f;
            if ((!in_dropdown && !in_hide) || !m_profile) break;

            for (auto& mp : m_profile->panels) {
                if (mp.metric_id != hit.metric_id) continue;
                if (in_hide) {
                    mp.visible = false;
                    RebuildClusters();
                    return;
                }
                VizType current = mp.viz_type;
                auto ov = m_viz_overrides.find(mp.metric_id);
                if (ov != m_viz_overrides.end()) current = ov->second;

                auto allowed = AllowedVizTypes(mp.cluster, mp.unit);
                if (allowed.empty()) allowed = {VizType::LineGraph, VizType::Numeral};
                auto it = std::find(allowed.begin(), allowed.end(), current);
                size_t next = (it == allowed.end()) ? 0 : ((size_t)std::distance(allowed.begin(), it) + 1) % allowed.size();
                m_viz_overrides[mp.metric_id] = allowed[next];
                return;
            }
        }
    }

    // Cluster header collapse?
    for (auto& hdr : m_clusters) {
        if (x >= m_page_x && x <= m_page_x + m_page_w &&
            cy >= hdr.y0 && cy <= hdr.y1) {
            hdr.collapsed = !hdr.collapsed;
            m_collapsed[hdr.cluster_id] = hdr.collapsed;
            return;
        }
    }

    // Viz dropdown trigger (▾ at top-right of any panel)?
    // Simplified: check if click is within 20px of panel top-right corner
    // (full hit-test requires storing per-panel rects — omitted for brevity)
}

void DashboardPage::OnScroll(float delta) {
    if (m_edit_mode) {
        m_edit_scroll_y -= delta * 44.0f;
        ClampEditScroll();
        return;
    }
    m_scroll_y -= delta * 40.0f;
    float max_scroll = std::max(0.0f, m_content_h - m_view_h);
    m_scroll_y = std::clamp(m_scroll_y, 0.0f, max_scroll);
}

void DashboardPage::OnMouseMove(float, float y) {
    if (!m_edit_scroll_dragging) return;
    ScrollEditToThumbY(y - m_edit_scroll_drag_offset);
}

void DashboardPage::OnMouseUp() {
    m_edit_scroll_dragging = false;
}

void DashboardPage::ClampEditScroll() {
    float view = std::max(1.0f, m_edit_h - 68.0f);
    float max_scroll = std::max(0.0f, m_edit_content_h - view);
    m_edit_scroll_y = std::clamp(m_edit_scroll_y, 0.0f, max_scroll);
}

void DashboardPage::DrawEditScrollbar(float x, float y, float w, float h) {
    m_edit_scroll_rail_x0 = m_edit_scroll_rail_y0 = m_edit_scroll_rail_x1 = m_edit_scroll_rail_y1 = 0.0f;
    m_edit_scroll_thumb_x0 = m_edit_scroll_thumb_y0 = m_edit_scroll_thumb_x1 = m_edit_scroll_thumb_y1 = 0.0f;
    if (m_edit_content_h <= h + 1.0f) return;
    auto* dc = m_ctx.DC();
    float rail_x = x + w - 16.0f;
    D2D1_RECT_F rail = {rail_x, y + 4, rail_x + 12.0f, y + h - 4};
    m_edit_scroll_rail_x0 = rail.left;
    m_edit_scroll_rail_y0 = rail.top;
    m_edit_scroll_rail_x1 = rail.right;
    m_edit_scroll_rail_y1 = rail.bottom;
    dc->FillRoundedRectangle(D2D1::RoundedRect(rail, 4, 4),
        m_ctx.BrushSolid({0.05f,0.05f,0.07f,1}));
    dc->DrawRoundedRectangle(D2D1::RoundedRect(rail, 5, 5),
        m_ctx.BrushSolid({0.55f,0.16f,0.18f,0.9f}), 0.75f);
    float track_h = rail.bottom - rail.top;
    float thumb_h = std::max(38.0f, track_h * (h / std::max(m_edit_content_h, 1.0f)));
    float max_scroll = std::max(1.0f, m_edit_content_h - h);
    float ty = rail.top + (track_h - thumb_h) * (m_edit_scroll_y / max_scroll);
    D2D1_RECT_F thumb = {rail.left + 2, ty, rail.right - 2, ty + thumb_h};
    m_edit_scroll_thumb_x0 = thumb.left;
    m_edit_scroll_thumb_y0 = thumb.top;
    m_edit_scroll_thumb_x1 = thumb.right;
    m_edit_scroll_thumb_y1 = thumb.bottom;
    dc->FillRoundedRectangle(D2D1::RoundedRect(thumb, 3, 3),
        m_ctx.BrushSolid({0.95f,0.24f,0.24f,0.95f}));
}

void DashboardPage::BeginEditSession() {
    if (!m_profile) return;
    m_edit_backup = *m_profile;
    m_has_edit_backup = true;
    m_edit_mode = true;
    m_edit_scroll_y = 0.0f;
    m_profile_save_requested = false;
}

void DashboardPage::CommitEditSession() {
    if (!m_profile) return;
    FlushToProfile();
    m_edit_mode = false;
    m_has_edit_backup = false;
    m_edit_scroll_dragging = false;
    RebuildClusters();
    m_profile_save_requested = true;
}

void DashboardPage::CancelEditSession() {
    if (m_profile && m_has_edit_backup) {
        std::string path = m_profile->FilePath();
        *m_profile = m_edit_backup;
        m_profile->SetFilePath(path);
    }
    m_viz_overrides.clear();
    m_edit_mode = false;
    m_has_edit_backup = false;
    m_edit_scroll_dragging = false;
    RebuildClusters();
    m_profile_save_requested = false;
}

void DashboardPage::ScrollEditToThumbY(float y) {
    float view = std::max(1.0f, m_edit_h - 68.0f);
    if (m_edit_content_h <= view + 1.0f) return;
    float track_h = m_edit_scroll_rail_y1 - m_edit_scroll_rail_y0;
    float thumb_h = m_edit_scroll_thumb_y1 - m_edit_scroll_thumb_y0;
    float usable = std::max(1.0f, track_h - thumb_h);
    float clamped_y = std::clamp(y, m_edit_scroll_rail_y0, m_edit_scroll_rail_y0 + usable);
    float frac = (clamped_y - m_edit_scroll_rail_y0) / usable;
    m_edit_scroll_y = frac * std::max(0.0f, m_edit_content_h - view);
    ClampEditScroll();
}

bool DashboardPage::ConsumeProfileSaveRequested() {
    bool requested = m_profile_save_requested;
    m_profile_save_requested = false;
    return requested;
}

void DashboardPage::FlushToProfile() {
    if (!m_profile) return;
    for (auto& mp : m_profile->panels) {
        auto it = m_viz_overrides.find(mp.metric_id);
        if (it != m_viz_overrides.end()) mp.viz_type = it->second;
    }
    m_viz_overrides.clear();
}

} // namespace Client
