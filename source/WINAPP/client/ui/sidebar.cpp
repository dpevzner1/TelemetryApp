#include "sidebar.h"
#include <dwrite.h>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Client {

static const NavItem s_items[] = {
    { NavPage::Dashboard, "Dashboard", "\xe2\x96\xa0" },
    { NavPage::Api,       "API Keys",  "\xf0\x9f\x94\x91" },
    { NavPage::Metrics,   "Metrics",   "\xe2\x96\xb2" },
    { NavPage::Fleet,     "Fleet",     "\xe2\x97\x8e" },
    { NavPage::Settings,  "Settings",  "\xe2\x9a\x99" },
};

static constexpr D2D1_COLOR_F kBg         = {0.10f, 0.10f, 0.12f, 1.0f};
static constexpr D2D1_COLOR_F kBgActive   = {0.16f, 0.16f, 0.20f, 1.0f};
static constexpr D2D1_COLOR_F kAccent     = {0.30f, 0.63f, 1.00f, 1.0f};
static constexpr D2D1_COLOR_F kText       = {0.88f, 0.88f, 0.90f, 1.0f};
static constexpr D2D1_COLOR_F kTextDim    = {0.50f, 0.50f, 0.54f, 1.0f};
static constexpr D2D1_COLOR_F kGreen      = {0.30f, 0.80f, 0.40f, 1.0f};
static constexpr D2D1_COLOR_F kRed        = {0.85f, 0.25f, 0.25f, 1.0f};
static constexpr D2D1_COLOR_F kLogoText   = {1.00f, 1.00f, 1.00f, 1.0f};

static std::wstring Widen(const std::string& s) {
    return std::wstring(s.begin(), s.end());
}

Sidebar::Sidebar(D2DContext& ctx) : m_ctx(ctx) {}

void Sidebar::DrawItem(float y, const NavItem& item, bool active, float /*dpi_scale*/) {
    auto* dc = m_ctx.DC();
    auto* br_bg     = m_ctx.BrushSolid(active ? kBgActive : kBg);
    auto* br_accent = m_ctx.BrushSolid(kAccent);

    D2D1_RECT_F bg = {0, y, WIDTH, y + ITEM_H};
    dc->FillRectangle(bg, br_bg);

    if (active) {
        D2D1_RECT_F bar = {0, y + 6.0f, 3.0f, y + ITEM_H - 6.0f};
        dc->FillRectangle(bar, br_accent);
    }

    D2D1_ELLIPSE dot = {{ITEM_PAD_L + 6.0f, y + ITEM_H / 2.0f}, 6.0f, 6.0f};
    dc->FillEllipse(dot, active ? br_accent : m_ctx.BrushSolid(kTextDim));

    std::wstring wlabel(item.label.begin(), item.label.end());
    m_ctx.DrawText(wlabel.c_str(),
        {ITEM_PAD_L + 20.0f, y + ITEM_PAD_T, WIDTH - 4.0f, y + ITEM_H - 4.0f},
        active ? kText : kTextDim, 13.0f);
}

void Sidebar::Draw(float height, float dpi_scale) {
    auto* dc = m_ctx.DC();
    auto* br_bg     = m_ctx.BrushSolid(kBg);
    auto* br_border = m_ctx.BrushSolid({0.20f, 0.20f, 0.24f, 1.0f});

    dc->FillRectangle({0, 0, WIDTH, height}, br_bg);

    std::wstring role = Widen(m_product_role);
    std::wstring ver = Widen(m_product_version);
    m_ctx.DrawText(L"TelemetryApp", {12.0f, 8.0f, WIDTH - 8.0f, 30.0f}, kLogoText, 14.0f, true);
    m_ctx.DrawText(role.c_str(),    {12.0f, 31.0f, WIDTH - 8.0f, 50.0f}, kAccent,   10.5f, true);
    m_ctx.DrawText(ver.c_str(),     {12.0f, 52.0f, WIDTH - 8.0f, 70.0f}, kTextDim,  10.0f, true);

    dc->DrawLine({0, LOGO_H}, {WIDTH, LOGO_H}, br_border, 0.5f);

    m_rects.clear();
    float y = LOGO_H + 8.0f;
    for (const auto& item : s_items) {
        if (item.page == NavPage::Fleet && !m_fleet_visible) continue;
        DrawItem(y, item, item.page == m_active, dpi_scale);
        m_rects.push_back({y, y + ITEM_H, item.page});
        y += ITEM_H;
    }

    float sy = height - STATUS_H;
    dc->DrawLine({0, sy}, {WIDTH, sy}, br_border, 0.5f);
    D2D1_COLOR_F status_col = service_connected ? kGreen : kRed;
    D2D1_ELLIPSE dot = {{14.0f, sy + 16.0f}, 5.0f, 5.0f};
    dc->FillEllipse(dot, m_ctx.BrushSolid(status_col));
    m_ctx.DrawText(service_connected ? L"Service: Online" : L"Service: Offline",
        {26.0f, sy + 7.0f, WIDTH - 4.0f, sy + 24.0f}, status_col, 11.0f);

    std::string addr = service_address.empty() ? "LAN: unavailable" : service_address;
    std::wstring waddr(addr.begin(), addr.end());
    m_ctx.DrawText(waddr.c_str(),
        {14.0f, sy + 27.0f, WIDTH - 6.0f, sy + 43.0f},
        service_connected ? kGreen : kTextDim, 10.0f);

    char buf[32]{};
    snprintf(buf, sizeof(buf), "%d key%s active", active_key_count, active_key_count != 1 ? "s" : "");
    std::wstring wbuf(buf, buf + strlen(buf));
    m_ctx.DrawText(wbuf.c_str(), {14.0f, sy + 45.0f, WIDTH - 4.0f, sy + STATUS_H - 4.0f},
                   kTextDim, 10.0f);

    dc->DrawLine({WIDTH - 0.5f, 0}, {WIDTH - 0.5f, height}, br_border, 1.0f);
}

bool Sidebar::HitTest(float x, float y, NavPage& out_page) const {
    if (x < 0 || x >= WIDTH) return false;
    for (const auto& r : m_rects) {
        if (y >= r.y0 && y < r.y1) {
            out_page = r.page;
            return true;
        }
    }
    return false;
}

void Sidebar::OnClick(float x, float y) {
    NavPage p;
    if (HitTest(x, y, p)) {
        m_active = p;
        if (m_on_nav) m_on_nav(p);
    }
}

} // namespace Client
