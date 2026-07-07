#include "metrics_page.h"
#include <d2d1_1helper.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace Client {

static constexpr D2D1_COLOR_F kBg     = {0.10f, 0.10f, 0.12f, 1.0f};
static constexpr D2D1_COLOR_F kPanel  = {0.14f, 0.14f, 0.18f, 1.0f};
static constexpr D2D1_COLOR_F kText   = {0.88f, 0.88f, 0.90f, 1.0f};
static constexpr D2D1_COLOR_F kDim    = {0.50f, 0.50f, 0.54f, 1.0f};
static constexpr D2D1_COLOR_F kGreen  = {0.30f, 0.80f, 0.40f, 1.0f};
static constexpr D2D1_COLOR_F kBlue   = {0.25f, 0.60f, 1.00f, 1.0f};
static constexpr D2D1_COLOR_F kBorder = {0.22f, 0.22f, 0.26f, 1.0f};
static constexpr D2D1_COLOR_F kHdr    = {0.12f, 0.12f, 0.16f, 1.0f};

MetricsPage::MetricsPage(D2DContext& ctx) : m_ctx(ctx) {
    m_groups = {
        {LogGroup::HighFreqNumerics, "High-Frequency Numerics",
            "CPU, GPU utilization, clocks, network bytes - default 1s interval",
            true, 1, false},
        {LogGroup::SlowState, "Slow-Changing State",
            "Temperatures, VRAM%, per-volume free% - default 10s interval",
            true, 10, false},
        {LogGroup::EventDriven, "Event-Driven",
            "Throttle flags, errors, drops - logged on value change only",
            true, 0, true},
        {LogGroup::ProcessWatcher, "Process Watcher",
            "Per-process metrics - enable per process in Settings",
            false, 1, false},
        {LogGroup::SelfMonitoring, "Self-Monitoring",
            "Service health, poll latency - always captured",
            true, 1, false},
    };
}

void MetricsPage::SetMetrics(std::vector<MetricDesc> metrics) {
    m_metrics = std::move(metrics);
    for (const auto& m : m_metrics) m_cluster_expanded.try_emplace(m.cluster, true);
}

void MetricsPage::SyncFromProfile(const std::vector<MetricPanel>& panels) {
    m_metrics.clear();
    m_metrics.reserve(panels.size());
    for (const auto& p : panels) {
        m_metrics.push_back({p.metric_id, p.label, p.cluster, p.unit, p.logged});
    }
    for (const auto& m : m_metrics) m_cluster_expanded.try_emplace(m.cluster, true);
}

void MetricsPage::SetHudMetricIds(const std::vector<uint32_t>& ids) {
    m_hud_metric_enabled.clear();
    for (uint32_t id : ids) m_hud_metric_enabled[id] = true;
    m_hud_all = !m_metrics.empty() && ids.size() >= m_metrics.size();
}

std::vector<uint32_t> MetricsPage::GetHudMetricIds() const {
    std::vector<uint32_t> out;
    for (const auto& m : m_metrics) {
        auto it = m_hud_metric_enabled.find(m.id);
        if (it != m_hud_metric_enabled.end() && it->second) out.push_back(m.id);
    }
    return out;
}

std::vector<uint32_t> MetricsPage::GetLoggedMetricIds() const {
    std::vector<uint32_t> out;
    for (const auto& m : m_metrics) {
        if (m.logged) out.push_back(m.id);
    }
    return out;
}

void MetricsPage::SetGroupEnabled(LogGroup g, bool enabled) {
    for (auto& gr : m_groups) {
        if (gr.group == g) {
            gr.enabled = enabled;
            break;
        }
    }
}

const char* MetricsPage::ClusterLabel(const std::string& id) {
    if (id == "cpu")       return "CPU";
    if (id == "cpu_cores") return "CPU Cores";
    if (id == "memory")    return "Memory";
    if (id == "gpu")       return "GPU";
    if (id == "disk")      return "Storage";
    if (id == "network")   return "Network";
    if (id == "temp")      return "Thermals";
    if (id == "process")   return "Process Watcher";
    if (id == "self")      return "Self-Monitoring";
    return id.c_str();
}

void MetricsPage::Draw(float x, float y, float w, float h, float) {
    auto* dc = m_ctx.DC();
    dc->FillRectangle({x, y, x + w, y + h}, m_ctx.BrushSolid(kBg));
    dc->PushAxisAlignedClip({x, y, x + w, y + h}, D2D1_ANTIALIAS_MODE_ALIASED);

    m_check_rects.clear();
    m_button_rects.clear();
    m_cluster_rects.clear();

    m_page_x = x;
    m_page_y = y;
    m_page_w = w;
    m_page_h = h;
    m_view_y = y + 116.0f;
    m_view_h = std::max(40.0f, h - 124.0f);

    m_ctx.DrawText(L"Metric Logging", {x + 20, y + 16, x + w - 20, y + 44}, kText, 18.0f, true);

    float tb_x = x + w - 260.0f;
    float tb_y = y + 16.0f;
    D2D1_RECT_F tb_r = {tb_x, tb_y, tb_x + 240.0f, tb_y + 30.0f};
    D2D1_COLOR_F tb_col = m_logging_enabled
        ? D2D1_COLOR_F{0.10f, 0.28f, 0.14f, 1.0f}
        : D2D1_COLOR_F{0.18f, 0.10f, 0.10f, 1.0f};
    dc->FillRoundedRectangle(D2D1::RoundedRect(tb_r, 6, 6), m_ctx.BrushSolid(tb_col));
    dc->DrawRoundedRectangle(D2D1::RoundedRect(tb_r, 6, 6),
        m_ctx.BrushSolid(m_logging_enabled ? kGreen : kDim), 1.0f);
    m_ctx.DrawText(m_logging_enabled ? L"Stop Logging" : L"Choose Folder + Start Logging",
                   tb_r, m_logging_enabled ? kGreen : kDim, 11.0f, true);
    m_button_rects.push_back({tb_r.left, tb_r.top, tb_r.right, tb_r.bottom, ACTION_LOGGING});

    float fy = y + 54.0f;
    m_ctx.DrawText(L"Output Format:", {x + 20, fy, x + 130, fy + 16}, kDim, 10.0f);
    const char* fmts[] = {"jsonl", "csv"};
    for (int i = 0; i < 2; ++i) {
        bool sel = m_log_format == fmts[i];
        D2D1_RECT_F fr = {x + 140.0f + i * 70.0f, fy - 2.0f, x + 205.0f + i * 70.0f, fy + 18.0f};
        dc->FillRoundedRectangle(D2D1::RoundedRect(fr, 4, 4),
            m_ctx.BrushSolid(sel ? D2D1_COLOR_F{0.12f,0.26f,0.44f,1} : kHdr));
        dc->DrawRoundedRectangle(D2D1::RoundedRect(fr, 4, 4),
            m_ctx.BrushSolid(sel ? kBlue : kBorder), sel ? 1.5f : 0.5f);
        std::string fs = fmts[i];
        std::wstring wf(fs.begin(), fs.end());
        m_ctx.DrawText(wf.c_str(), fr, sel ? kBlue : kDim, 10.0f, sel);
        m_button_rects.push_back({fr.left, fr.top, fr.right, fr.bottom,
                                  i == 0 ? ACTION_FORMAT_JSONL : ACTION_FORMAT_CSV});
    }

    std::wstring dir = m_log_dir.empty()
        ? L"No log folder selected"
        : std::wstring(m_log_dir.begin(), m_log_dir.end());
    m_ctx.DrawText(L"Log Folder:", {x + 20, y + 78, x + 92, y + 96}, kDim, 10.0f, true);
    m_ctx.DrawText(dir.c_str(), {x + 96, y + 78, x + w - 590, y + 98}, kDim, 10.0f);

    auto draw_btn = [&](float bx, float by, float bw, const wchar_t* label, int action, bool strong) {
        D2D1_RECT_F br = {bx, by, bx + bw, by + 24.0f};
        dc->FillRoundedRectangle(D2D1::RoundedRect(br, 5, 5),
            m_ctx.BrushSolid(strong ? D2D1_COLOR_F{0.12f,0.26f,0.44f,1} : kHdr));
        dc->DrawRoundedRectangle(D2D1::RoundedRect(br, 5, 5),
            m_ctx.BrushSolid(strong ? kBlue : kBorder), 1.0f);
        m_ctx.DrawText(label, br, strong ? kBlue : kText, 10.0f, true);
        m_button_rects.push_back({br.left, br.top, br.right, br.bottom, action});
    };
    draw_btn(x + w - 568.0f, y + 76.0f, 128.0f, L"Default Display", ACTION_DEFAULT_DISPLAY, false);
    draw_btn(x + w - 432.0f, y + 76.0f, 104.0f, L"Display All", ACTION_DISPLAY_ALL, true);
    draw_btn(x + w - 320.0f, y + 76.0f, 96.0f, L"BAR All", ACTION_HUD_ALL, m_hud_all);

    float cy = m_view_y - m_scroll_y;
    DrawLogGroupSection(x, cy, w);

    std::vector<std::string> clusters;
    for (const auto& m : m_metrics) {
        if (std::find(clusters.begin(), clusters.end(), m.cluster) == clusters.end()) {
            clusters.push_back(m.cluster);
        }
    }
    for (const auto& c : clusters) DrawMetricCluster(x, cy, w, c);

    m_content_h = (cy + m_scroll_y) - m_view_y + 8.0f;
    ClampScroll();
    DrawScrollbar(x, m_view_y, w, m_view_h);

    dc->PopAxisAlignedClip();
}

void MetricsPage::DrawLogGroupSection(float x, float& cy, float w) {
    auto* dc = m_ctx.DC();
    cy += 8.0f;
    m_ctx.DrawText(L"LOGGING GROUPS", {x + 20, cy, x + w - 20, cy + 16}, kDim, 10.0f, true);
    cy += 20.0f;

    for (int i = 0; i < (int)m_groups.size(); ++i) {
        auto& gr = m_groups[i];
        D2D1_RECT_F row = {x + 20, cy, x + w - 20, cy + 44};
        dc->FillRoundedRectangle(D2D1::RoundedRect(row, 5, 5), m_ctx.BrushSolid(kPanel));
        dc->DrawRoundedRectangle(D2D1::RoundedRect(row, 5, 5), m_ctx.BrushSolid(kBorder), 0.5f);

        DrawCheckRow(x + 30, cy + 12, 20, gr.enabled, "", "");
        m_check_rects.push_back({x + 30, cy + 12, x + 50, cy + 32, UINT32_MAX, i, ""});

        std::wstring wl(gr.label.begin(), gr.label.end());
        m_ctx.DrawText(wl.c_str(), {x + 56, cy + 6, x + w - 130, cy + 26},
                       gr.enabled ? kText : kDim, 11.0f, gr.enabled);

        if (!gr.on_change_only) {
            char buf[32]{};
            snprintf(buf, sizeof(buf), "%ds interval", gr.interval_sec);
            std::wstring wi(buf, buf + strlen(buf));
            m_ctx.DrawText(wi.c_str(), {x + w - 124, cy + 8, x + w - 24, cy + 26}, kDim, 9.0f);
        } else {
            m_ctx.DrawText(L"on change", {x + w - 124, cy + 8, x + w - 24, cy + 26}, kDim, 9.0f);
        }

        std::wstring wd(gr.description.begin(), gr.description.end());
        m_ctx.DrawText(wd.c_str(), {x + 56, cy + 26, x + w - 24, cy + 42}, kDim, 9.0f);
        cy += 50.0f;
    }
    cy += 12.0f;
}

void MetricsPage::DrawMetricCluster(float x, float& cy, float w, const std::string& cluster) {
    auto* dc = m_ctx.DC();
    const char* label = ClusterLabel(cluster);
    bool expanded = m_cluster_expanded.count(cluster) ? m_cluster_expanded.at(cluster) : true;

    int total = 0, on = 0, bar_on = 0;
    for (const auto& m : m_metrics) {
        if (m.cluster != cluster) continue;
        ++total;
        if (m.logged) ++on;
        if (m_hud_metric_enabled[m.id]) ++bar_on;
    }

    D2D1_RECT_F hdr = {x + 20, cy, x + w - 20, cy + 30};
    dc->FillRoundedRectangle(D2D1::RoundedRect(hdr, 5, 5), m_ctx.BrushSolid(kPanel));
    dc->DrawRoundedRectangle(D2D1::RoundedRect(hdr, 5, 5), m_ctx.BrushSolid(kBorder), 0.5f);

    std::wstring wl = std::wstring(expanded ? L"v " : L"> ") + std::wstring(label, label + strlen(label));
    m_ctx.DrawText(wl.c_str(), {x + 30, cy + 6, x + w - 240, cy + 28}, kText, 11.0f, true);
    m_cluster_rects.push_back({hdr.left, hdr.top, hdr.right - 260.0f, hdr.bottom, cluster});

    char cnt[24]{};
    snprintf(cnt, sizeof(cnt), "%d/%d", on, total);
    std::wstring wc(cnt, cnt + strlen(cnt));
    m_ctx.DrawText(wc.c_str(), {x + w - 284, cy + 8, x + w - 230, cy + 26}, kDim, 10.0f);

    bool bar_all = total > 0 && bar_on == total;
    DrawCheckRow(x + w - 220, cy + 8, 14, bar_all, "", "");
    m_ctx.DrawText(L"BAR", {x + w - 202, cy + 6, x + w - 166, cy + 28}, kDim, 10.0f);
    m_check_rects.push_back({x + w - 220, cy + 8, x + w - 206, cy + 22, UINT32_MAX, -4, cluster});

    bool all_on = total > 0 && on == total;
    DrawCheckRow(x + w - 100, cy + 8, 14, all_on, "", "");
    m_ctx.DrawText(L"ALL", {x + w - 82, cy + 6, x + w - 24, cy + 28}, kDim, 10.0f);
    m_check_rects.push_back({x + w - 100, cy + 8, x + w - 86, cy + 22, UINT32_MAX, -2, cluster});

    cy += 34.0f;
    if (!expanded) return;

    for (auto& m : m_metrics) {
        if (m.cluster != cluster) continue;
        DrawCheckRow(x + 36, cy + 4, 14, m.logged, m.label, m.unit);
        m_check_rects.push_back({x + 36, cy + 4, x + 50, cy + 18, m.id, -1, ""});

        bool hud_on = m_hud_metric_enabled[m.id];
        DrawCheckRow(x + w - 220, cy + 4, 14, hud_on, "", "");
        m_ctx.DrawText(L"BAR", {x + w - 202, cy + 1, x + w - 166, cy + 22}, kDim, 9.0f);
        m_check_rects.push_back({x + w - 220, cy + 4, x + w - 206, cy + 18, m.id, -3, ""});
        cy += 24.0f;
    }
    cy += 8.0f;
}

void MetricsPage::DrawCheckRow(float x, float y, float size, bool checked,
                               const std::string& label, const std::string& unit) {
    auto* dc = m_ctx.DC();
    D2D1_RECT_F box = {x, y, x + size, y + size};
    dc->FillRoundedRectangle(D2D1::RoundedRect(box, 3, 3),
        m_ctx.BrushSolid(checked ? D2D1_COLOR_F{0.10f,0.30f,0.18f,1}
                                 : D2D1_COLOR_F{0.10f,0.10f,0.13f,1}));
    dc->DrawRoundedRectangle(D2D1::RoundedRect(box, 3, 3),
        m_ctx.BrushSolid(checked ? kGreen : kBorder), 1.0f);
    if (checked) m_ctx.DrawText(L"v", box, kGreen, size * 0.7f, true);

    if (!label.empty()) {
        float tx = x + size + 6.0f;
        std::wstring wl(label.begin(), label.end());
        m_ctx.DrawText(wl.c_str(), {tx, y - 2, tx + 200, y + size + 2}, kText, 10.0f);
        if (!unit.empty()) {
            std::wstring wu = L" (" + std::wstring(unit.begin(), unit.end()) + L")";
            m_ctx.DrawText(wu.c_str(), {tx + 200, y - 2, tx + 280, y + size + 2}, kDim, 9.0f);
        }
    }
}

void MetricsPage::OnClick(float cx, float cy) {
    if (m_content_h > m_view_h + 1.0f &&
        cx >= m_scroll_rail_x0 && cx <= m_scroll_rail_x1 &&
        cy >= m_scroll_rail_y0 && cy <= m_scroll_rail_y1) {
        if (cy >= m_scroll_thumb_y0 && cy <= m_scroll_thumb_y1) {
            m_scroll_dragging = true;
            m_scroll_drag_offset = cy - m_scroll_thumb_y0;
        } else {
            ScrollToThumbY(cy);
        }
        return;
    }

    for (const auto& br : m_button_rects) {
        if (cx < br.x0 || cx > br.x1 || cy < br.y0 || cy > br.y1) continue;
        switch (br.action) {
        case ACTION_LOGGING:
            if (!m_logging_enabled && m_on_logging_enable_requested) {
                if (!m_on_logging_enable_requested(*this)) return;
            }
            m_logging_enabled = !m_logging_enabled;
            return;
        case ACTION_FORMAT_JSONL: m_log_format = "jsonl"; return;
        case ACTION_FORMAT_CSV:   m_log_format = "csv"; return;
        case ACTION_DISPLAY_ALL:
            for (auto& m : m_metrics) {
                m.logged = true;
                m_hud_metric_enabled[m.id] = true;
            }
            m_hud_all = true;
            NotifyHudChanged();
            return;
        case ACTION_DEFAULT_DISPLAY:
            for (auto& m : m_metrics) m.logged = true;
            m_hud_metric_enabled.clear();
            m_hud_all = false;
            NotifyHudChanged();
            return;
        case ACTION_HUD_ALL:
            m_hud_all = !m_hud_all;
            for (const auto& m : m_metrics) m_hud_metric_enabled[m.id] = m_hud_all;
            NotifyHudChanged();
            return;
        }
    }

    for (const auto& cr : m_check_rects) {
        if (cx < cr.x0 || cx > cr.x1 || cy < cr.y0 || cy > cr.y1) continue;
        if (cr.group_idx == -1 && cr.metric_id != UINT32_MAX) {
            for (auto& m : m_metrics) {
                if (m.id == cr.metric_id) {
                    m.logged = !m.logged;
                    break;
                }
            }
        } else if (cr.group_idx >= 0 && cr.group_idx < (int)m_groups.size()) {
            m_groups[cr.group_idx].enabled = !m_groups[cr.group_idx].enabled;
        } else if (cr.group_idx == -2 && cr.metric_id == UINT32_MAX) {
            bool all_on = true;
            for (const auto& m : m_metrics) {
                if (m.cluster == cr.cluster && !m.logged) { all_on = false; break; }
            }
            for (auto& m : m_metrics) {
                if (m.cluster == cr.cluster) m.logged = !all_on;
            }
        } else if (cr.group_idx == -3 && cr.metric_id != UINT32_MAX) {
            m_hud_metric_enabled[cr.metric_id] = !m_hud_metric_enabled[cr.metric_id];
            m_hud_all = GetHudMetricIds().size() >= m_metrics.size();
            NotifyHudChanged();
        } else if (cr.group_idx == -4 && cr.metric_id == UINT32_MAX) {
            bool all_on = true;
            for (const auto& m : m_metrics) {
                if (m.cluster == cr.cluster && !m_hud_metric_enabled[m.id]) { all_on = false; break; }
            }
            for (const auto& m : m_metrics) {
                if (m.cluster == cr.cluster) m_hud_metric_enabled[m.id] = !all_on;
            }
            m_hud_all = GetHudMetricIds().size() >= m_metrics.size();
            NotifyHudChanged();
        }
        return;
    }

    for (const auto& cr : m_cluster_rects) {
        if (cx >= cr.x0 && cx <= cr.x1 && cy >= cr.y0 && cy <= cr.y1) {
            m_cluster_expanded[cr.cluster] = !m_cluster_expanded[cr.cluster];
            return;
        }
    }
}

void MetricsPage::OnScroll(float delta) {
    m_scroll_y -= delta * 44.0f;
    ClampScroll();
}

void MetricsPage::OnMouseMove(float, float y) {
    if (!m_scroll_dragging) return;
    ScrollToThumbY(y - m_scroll_drag_offset);
}

void MetricsPage::OnMouseUp() {
    m_scroll_dragging = false;
}

void MetricsPage::ClampScroll() {
    float max_scroll = std::max(0.0f, m_content_h - m_view_h);
    m_scroll_y = std::clamp(m_scroll_y, 0.0f, max_scroll);
}

void MetricsPage::DrawScrollbar(float x, float y, float w, float h) {
    m_scroll_rail_x0 = m_scroll_rail_y0 = m_scroll_rail_x1 = m_scroll_rail_y1 = 0.0f;
    m_scroll_thumb_x0 = m_scroll_thumb_y0 = m_scroll_thumb_x1 = m_scroll_thumb_y1 = 0.0f;
    if (m_content_h <= m_view_h + 1.0f) return;
    auto* dc = m_ctx.DC();
    float rail_w = 10.0f;
    float rail_x = x + w - 16.0f;
    D2D1_RECT_F rail = {rail_x, y + 4.0f, rail_x + rail_w, y + h - 4.0f};
    m_scroll_rail_x0 = rail.left;
    m_scroll_rail_y0 = rail.top;
    m_scroll_rail_x1 = rail.right;
    m_scroll_rail_y1 = rail.bottom;
    dc->FillRoundedRectangle(D2D1::RoundedRect(rail, 5, 5),
        m_ctx.BrushSolid({0.06f,0.06f,0.08f,1.0f}));
    dc->DrawRoundedRectangle(D2D1::RoundedRect(rail, 5, 5),
        m_ctx.BrushSolid({0.28f,0.28f,0.34f,1.0f}), 0.75f);

    float track_h = rail.bottom - rail.top;
    float thumb_h = std::max(42.0f, track_h * (m_view_h / std::max(m_content_h, 1.0f)));
    float max_scroll = std::max(1.0f, m_content_h - m_view_h);
    float thumb_y = rail.top + (track_h - thumb_h) * (m_scroll_y / max_scroll);
    D2D1_RECT_F thumb = {rail.left + 2.0f, thumb_y, rail.right - 2.0f, thumb_y + thumb_h};
    m_scroll_thumb_x0 = thumb.left;
    m_scroll_thumb_y0 = thumb.top;
    m_scroll_thumb_x1 = thumb.right;
    m_scroll_thumb_y1 = thumb.bottom;
    dc->FillRoundedRectangle(D2D1::RoundedRect(thumb, 4, 4),
        m_ctx.BrushSolid({0.25f,0.60f,1.00f,0.95f}));
}

void MetricsPage::ScrollToThumbY(float y) {
    if (m_content_h <= m_view_h + 1.0f) return;
    float track_h = m_scroll_rail_y1 - m_scroll_rail_y0;
    float thumb_h = m_scroll_thumb_y1 - m_scroll_thumb_y0;
    float usable = std::max(1.0f, track_h - thumb_h);
    float clamped_y = std::clamp(y, m_scroll_rail_y0, m_scroll_rail_y0 + usable);
    float frac = (clamped_y - m_scroll_rail_y0) / usable;
    m_scroll_y = frac * std::max(0.0f, m_content_h - m_view_h);
    ClampScroll();
}

void MetricsPage::NotifyHudChanged() {
    if (m_on_hud_metrics_changed) m_on_hud_metrics_changed(GetHudMetricIds());
}

} // namespace Client
