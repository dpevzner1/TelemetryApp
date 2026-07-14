#pragma once
#include "../renderer/d2d_context.h"
#include <d2d1_1.h>
#include <string>
#include <vector>
#include <functional>
#include <utility>

namespace Client {

enum class NavPage : int {
    Dashboard   = 0,
    Api         = 1,
    Metrics     = 2,
    Fleet       = 3,
    Settings    = 4,
    Count
};

struct NavItem {
    NavPage     page;
    std::string label;
    std::string icon;   // unicode symbol
};

class Sidebar {
public:
    explicit Sidebar(D2DContext& ctx);

    // Width of the sidebar in logical pixels
    static constexpr float WIDTH = 180.0f;

    void SetPage(NavPage p)            { m_active = p; }
    NavPage ActivePage() const         { return m_active; }
    void SetFleetVisible(bool visible) { m_fleet_visible = visible; if (!visible && m_active == NavPage::Fleet) m_active = NavPage::Dashboard; }
    void SetProductIdentity(std::string role, std::string version) {
        m_product_role = std::move(role);
        m_product_version = std::move(version);
    }

    // Callback: called when user clicks a nav item
    using OnNavCb = std::function<void(NavPage)>;
    void SetOnNav(OnNavCb cb)          { m_on_nav = std::move(cb); }

    void Draw(float height, float dpi_scale);
    bool HitTest(float x, float y, NavPage& out_page) const;
    void OnClick(float x, float y);

    // Status bar fields (set by window each tick)
    bool   service_connected = false;
    bool   recording_active = false;
    int    active_key_count  = 0;
    std::string service_address;

private:
    D2DContext& m_ctx;
    NavPage     m_active = NavPage::Dashboard;
    bool        m_fleet_visible = false;
    std::string m_product_role = "Local Monitor";
    std::string m_product_version = "v0.0.0";
    OnNavCb     m_on_nav;

    struct ItemRect { float y0, y1; NavPage page; };
    std::vector<ItemRect> m_rects;

    static constexpr float ITEM_H      = 44.0f;
    static constexpr float ITEM_PAD_L  = 20.0f;
    static constexpr float ITEM_PAD_T  = 12.0f;
    static constexpr float LOGO_H      = 76.0f;
    static constexpr float STATUS_H    = 68.0f;

    void DrawItem(float y, const NavItem& item, bool active, float dpi_scale);
};

} // namespace Client
