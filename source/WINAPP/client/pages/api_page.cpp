#include "api_page.h"
#include <d2d1_1helper.h>
#include <ctime>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace Client {

static constexpr D2D1_COLOR_F kBg     = {0.10f, 0.10f, 0.12f, 1.0f};
static constexpr D2D1_COLOR_F kPanel  = {0.14f, 0.14f, 0.18f, 1.0f};
static constexpr D2D1_COLOR_F kText   = {0.88f, 0.88f, 0.90f, 1.0f};
static constexpr D2D1_COLOR_F kDim    = {0.50f, 0.50f, 0.54f, 1.0f};
static constexpr D2D1_COLOR_F kGreen  = {0.30f, 0.80f, 0.40f, 1.0f};
static constexpr D2D1_COLOR_F kRed    = {0.85f, 0.25f, 0.25f, 1.0f};
static constexpr D2D1_COLOR_F kBlue   = {0.25f, 0.60f, 1.00f, 1.0f};
static constexpr D2D1_COLOR_F kBorder = {0.22f, 0.22f, 0.26f, 1.0f};

ApiPage::ApiPage(D2DContext& ctx) : m_ctx(ctx) {}

void ApiPage::SetKeys(std::vector<ApiKeyInfo> keys) { m_keys = std::move(keys); }

std::string ApiPage::FmtMs(int64_t ms) {
    if (ms <= 0) return "—";
    time_t t = ms / 1000;
    char buf[32]{};
    struct tm tm_val{};
    gmtime_s(&tm_val, &t);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M UTC", &tm_val);
    return buf;
}

const char* ApiPage::ExpiryLabel(int t) {
    switch (t) {
    case 0: return "Permanent";
    case 1: return "Session";
    case 2: return "7 Days";
    case 3: return "30 Days";
    case 4: return "Custom Date";
    default: return "Unknown";
    }
}

// ── Draw ──────────────────────────────────────────────────────────────────────

void ApiPage::Draw(float x, float y, float w, float h, float /*dpi*/) {
    auto* dc = m_ctx.DC();

    dc->FillRectangle({x, y, x + w, y + h}, m_ctx.BrushSolid(kBg));

    // Page title
    m_ctx.DrawText(L"API Key Management",
        {x + 20, y + 16, x + w - 20, y + 44}, kText, 18.0f, true);

    m_ctx.DrawText(L"Keys authenticate all REST and SSE endpoints (X-API-Key header). "
                    L"Plaintext is shown only at creation — copy it immediately.",
        {x + 20, y + 46, x + w - 20, y + 66}, kDim, 11.0f);

    // Create button
    m_create_btn_x = x + w - 160;
    m_create_btn_y = y + 16;
    m_create_btn_w = 140;
    m_create_btn_h = 32;
    D2D1_RECT_F btn = {m_create_btn_x, m_create_btn_y,
                       m_create_btn_x + m_create_btn_w, m_create_btn_y + m_create_btn_h};
    dc->FillRoundedRectangle(D2D1::RoundedRect(btn, 6, 6), m_ctx.BrushSolid(kBlue));
    m_ctx.DrawText(L"+ New Key", btn, {1,1,1,1}, 12.0f, true);

    // New key banner
    float banner_y = y + 72;
    if (m_show_banner && !m_last_new_key.empty()) {
        DrawNewKeyBanner(x, banner_y, w);
        banner_y += 56;
    }

    // Table header
    DrawTable(x, banner_y + 8, w, h - (banner_y - y) - 16);

    // Create dialog overlay
    if (m_dialog_open) {
        DrawCreateDialog(x + w * 0.25f, y + h * 0.20f, w * 0.50f, h * 0.55f);
    }
}

void ApiPage::DrawNewKeyBanner(float x, float y, float w) {
    auto* dc = m_ctx.DC();
    D2D1_RECT_F bg = {x + 20, y, x + w - 20, y + 48};
    dc->FillRoundedRectangle(D2D1::RoundedRect(bg, 6, 6),
        m_ctx.BrushSolid({0.10f, 0.28f, 0.15f, 1.0f}));
    dc->DrawRoundedRectangle(D2D1::RoundedRect(bg, 6, 6),
        m_ctx.BrushSolid(kGreen), 1.0f);
    m_ctx.DrawText(L"✓  New key created — copy it now. It will not be shown again.",
        {bg.left + 12, y + 4, bg.right - 50, y + 24}, kGreen, 11.0f, true);
    // Show masked key
    std::string display = m_last_new_key.substr(0, 20) + "...";
    std::wstring wd(display.begin(), display.end());
    m_ctx.DrawText(wd.c_str(), {bg.left + 12, y + 26, bg.right - 50, y + 44},
                   {0.80f,0.90f,0.80f,1.0f}, 11.0f);
    // Dismiss X
    m_ctx.DrawText(L"✕", {bg.right - 44, y + 4, bg.right - 4, y + 44}, kDim, 14.0f, true);
}

void ApiPage::DrawTable(float x, float y, float w, float /*h*/) {
    auto* dc = m_ctx.DC();
    m_row_rects.clear();

    // Header row
    float hx = x + 20, hy = y;
    float colW[] = {w * 0.17f, w * 0.22f, w * 0.16f, w * 0.14f, w * 0.14f, w * 0.09f, w * 0.08f};
    const wchar_t* hdrs[] = {L"Name", L"Key Prefix", L"Created", L"Expiry", L"Expires", L"Status", L"Actions"};
    for (int i = 0; i < 7; ++i) {
        m_ctx.DrawText(hdrs[i], {hx, hy, hx + colW[i] - 4, hy + 20}, kDim, 10.0f);
        hx += colW[i];
    }

    dc->DrawLine({x + 20, hy + 22}, {x + w - 20, hy + 22},
                 m_ctx.BrushSolid(kBorder), 0.5f);

    float ry = hy + 28;
    for (const auto& k : m_keys) {
        float rowH = 38.0f;

        // Alternating row bg
        bool even = (&k - m_keys.data()) % 2 == 0;
        if (even) dc->FillRectangle({x + 20, ry, x + w - 20, ry + rowH},
            m_ctx.BrushSolid({0.12f,0.12f,0.15f,0.5f}));

        hx = x + 20;
        D2D1_COLOR_F sc = k.status == "Active" ? kGreen : kRed;

        // Name
        std::wstring wn(k.name.begin(), k.name.end());
        m_ctx.DrawText(wn.c_str(), {hx, ry + 8, hx + colW[0] - 4, ry + rowH}, kText, 11.0f);
        hx += colW[0];

        // Prefix
        std::wstring wp = L"`" + std::wstring(k.key_prefix.begin(), k.key_prefix.end()) + L"...`";
        m_ctx.DrawText(wp.c_str(), {hx, ry + 8, hx + colW[1] - 4, ry + rowH},
                       {0.70f,0.85f,0.70f,1.0f}, 10.0f);
        hx += colW[1];

        // Created
        std::string sc2 = FmtMs(k.created_at_ms);
        std::wstring wc(sc2.begin(), sc2.end());
        m_ctx.DrawText(wc.c_str(), {hx, ry + 8, hx + colW[2] - 4, ry + rowH}, kDim, 10.0f);
        hx += colW[2];

        // Expiry type
        const char* el = ExpiryLabel(k.expiry_type);
        std::wstring we(el, el + strlen(el));
        m_ctx.DrawText(we.c_str(), {hx, ry + 8, hx + colW[3] - 4, ry + rowH}, kDim, 10.0f);
        hx += colW[3];

        // Expires at
        std::string se = FmtMs(k.expires_at_ms);
        std::wstring wex(se.begin(), se.end());
        m_ctx.DrawText(wex.c_str(), {hx, ry + 8, hx + colW[4] - 4, ry + rowH}, kDim, 10.0f);
        hx += colW[4];

        // Status badge
        D2D1_RECT_F badge = {hx, ry + 9, hx + colW[5] - 8, ry + 27};
        dc->FillRoundedRectangle(D2D1::RoundedRect(badge, 4, 4),
            m_ctx.BrushSolid(k.status == "Active"
                ? D2D1_COLOR_F{0.10f,0.25f,0.12f,1} : D2D1_COLOR_F{0.25f,0.10f,0.10f,1}));
        std::wstring ws(k.status.begin(), k.status.end());
        m_ctx.DrawText(ws.c_str(), badge, sc, 9.0f, true);
        hx += colW[5];

        // Action buttons
        float bx = hx;
        // Rotate
        D2D1_RECT_F rb = {bx, ry + 8, bx + 28, ry + 28};
        dc->FillRoundedRectangle(D2D1::RoundedRect(rb, 4, 4),
            m_ctx.BrushSolid({0.18f,0.30f,0.44f,1}));
        m_ctx.DrawText(L"↻", rb, kBlue, 11.0f, true);
        m_row_rects.push_back({rb.left, rb.right, rb.top, rb.bottom, k.id, false, true});
        bx += 32;

        // Delete
        D2D1_RECT_F db = {bx, ry + 8, bx + 28, ry + 28};
        dc->FillRoundedRectangle(D2D1::RoundedRect(db, 4, 4),
            m_ctx.BrushSolid({0.35f,0.12f,0.12f,1}));
        m_ctx.DrawText(L"✕", db, kRed, 11.0f, true);
        m_row_rects.push_back({db.left, db.right, db.top, db.bottom, k.id, true, false});

        ry += rowH + 2;
        dc->DrawLine({x + 20, ry}, {x + w - 20, ry}, m_ctx.BrushSolid(kBorder), 0.5f);
        ry += 2;
    }

    if (m_keys.empty()) {
        m_ctx.DrawText(L"No API keys configured. Click “+ New Key” to create one.",
            {x + 20, ry + 20, x + w - 20, ry + 50}, kDim, 12.0f);
    }
}

void ApiPage::DrawCreateDialog(float x, float y, float w, float h) {
    m_dialog_x = x; m_dialog_y = y; m_dialog_w = w; m_dialog_h = h;
    auto* dc = m_ctx.DC();
    D2D1_RECT_F bg = {x, y, x + w, y + h};
    dc->FillRoundedRectangle(D2D1::RoundedRect(bg, 10, 10),
        m_ctx.BrushSolid({0.12f,0.12f,0.16f,0.98f}));
    dc->DrawRoundedRectangle(D2D1::RoundedRect(bg, 10, 10),
        m_ctx.BrushSolid(kBlue), 1.0f);

    m_ctx.DrawText(L"Create New API Key",
        {x + 20, y + 14, x + w - 20, y + 38}, kText, 14.0f, true);
    dc->DrawLine({x + 20, y + 42}, {x + w - 20, y + 42},
                 m_ctx.BrushSolid(kBorder), 0.5f);

    float fy = y + 52;
    m_ctx.DrawText(L"Key Name / Label:", {x + 20, fy, x + w - 20, fy + 16}, kDim, 10.0f);
    fy += 18;
    D2D1_RECT_F inp = {x + 20, fy, x + w - 20, fy + 28};
    dc->FillRoundedRectangle(D2D1::RoundedRect(inp, 4, 4),
        m_ctx.BrushSolid({0.08f,0.08f,0.10f,1}));
    dc->DrawRoundedRectangle(D2D1::RoundedRect(inp, 4, 4),
        m_ctx.BrushSolid(m_dlg_name_focus ? kBlue : kBorder), 1.0f);
    std::wstring wname(m_dlg_name.begin(), m_dlg_name.end());
    wname += m_dlg_name_focus ? L"|" : L"";
    m_ctx.DrawText(wname.c_str(), {inp.left + 6, fy + 4, inp.right - 4, fy + 26}, kText, 11.0f);
    fy += 36;

    m_ctx.DrawText(L"Expiry:", {x + 20, fy, x + w - 20, fy + 16}, kDim, 10.0f);
    fy += 18;
    const char* expiry_labels[] = {"Permanent","Session","7 Days","30 Days","Custom Date"};
    float bw = (w - 44.0f) / 5;
    for (int i = 0; i < 5; ++i) {
        D2D1_RECT_F eb = {x + 22 + bw * i, fy, x + 22 + bw * (i+1) - 4, fy + 26};
        m_expiry_x[i] = eb.left; m_expiry_y[i] = eb.top;
        m_expiry_w[i] = eb.right - eb.left; m_expiry_h[i] = eb.bottom - eb.top;
        bool sel = m_dlg_expiry_type == i;
        dc->FillRoundedRectangle(D2D1::RoundedRect(eb, 4, 4),
            m_ctx.BrushSolid(sel ? D2D1_COLOR_F{0.15f,0.28f,0.45f,1} : D2D1_COLOR_F{0.10f,0.10f,0.13f,1}));
        dc->DrawRoundedRectangle(D2D1::RoundedRect(eb, 4, 4),
            m_ctx.BrushSolid(sel ? kBlue : kBorder), sel ? 1.5f : 0.5f);
        std::string el = expiry_labels[i];
        std::wstring we(el.begin(), el.end());
        m_ctx.DrawText(we.c_str(), eb, sel ? kBlue : kDim, 9.0f, sel);
    }
    fy += 34;

    if (m_dlg_expiry_type == 4) {
        // Calendar date picker (simplified: text fields for Y/M/D)
        m_ctx.DrawText(L"Expiry Date (UTC):", {x + 20, fy, x + w - 20, fy + 14}, kDim, 10.0f);
        fy += 16;
        struct tm now_tm{};
        time_t now_t = m_dlg_custom_ms / 1000;
        if (now_t <= 0) { time(&now_t); now_t += 7 * 24 * 3600; }
        gmtime_s(&now_tm, &now_t);
        char date_buf[32]{};
        strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &now_tm);
        std::wstring wdate(date_buf, date_buf + strlen(date_buf));
        D2D1_RECT_F de = {x + 20, fy, x + 160, fy + 26};
        dc->FillRoundedRectangle(D2D1::RoundedRect(de, 4, 4), m_ctx.BrushSolid({0.08f,0.08f,0.10f,1}));
        dc->DrawRoundedRectangle(D2D1::RoundedRect(de, 4, 4), m_ctx.BrushSolid(kBlue), 1.0f);
        m_ctx.DrawText(wdate.c_str(), {de.left + 6, fy + 4, de.right, fy + 26}, kText, 11.0f);
        m_ctx.DrawText(L"← → to adjust", {x + 168, fy + 4, x + w - 20, fy + 26}, kDim, 9.0f);
        fy += 34;
    }

    // Buttons
    float by = y + h - 44;
    D2D1_RECT_F cancel_b = {x + w - 200, by, x + w - 116, by + 30};
    D2D1_RECT_F create_b = {x + w - 110, by, x + w - 20, by + 30};
    m_cancel_x = cancel_b.left; m_cancel_y = cancel_b.top;
    m_cancel_w = cancel_b.right - cancel_b.left; m_cancel_h = cancel_b.bottom - cancel_b.top;
    m_confirm_x = create_b.left; m_confirm_y = create_b.top;
    m_confirm_w = create_b.right - create_b.left; m_confirm_h = create_b.bottom - create_b.top;
    dc->FillRoundedRectangle(D2D1::RoundedRect(cancel_b, 5, 5), m_ctx.BrushSolid({0.15f,0.15f,0.18f,1}));
    dc->DrawRoundedRectangle(D2D1::RoundedRect(cancel_b, 5, 5), m_ctx.BrushSolid(kBorder), 1.0f);
    m_ctx.DrawText(L"Cancel", cancel_b, kDim, 11.0f, true);
    dc->FillRoundedRectangle(D2D1::RoundedRect(create_b, 5, 5), m_ctx.BrushSolid(kBlue));
    m_ctx.DrawText(L"Create", create_b, {1,1,1,1}, 11.0f, true);
}

// ── Interaction ───────────────────────────────────────────────────────────────

void ApiPage::OnClick(float cx, float cy) {
    // New key button
    if (cx >= m_create_btn_x && cx <= m_create_btn_x + m_create_btn_w &&
        cy >= m_create_btn_y && cy <= m_create_btn_y + m_create_btn_h) {
        m_dialog_open = true;
        m_dlg_name.clear();
        m_dlg_expiry_type = 0;
        m_dlg_custom_ms   = 0;
        m_dlg_name_focus  = true;
        return;
    }

    // Dismiss banner X (rough hit test — banner at top of table area)
    if (m_show_banner && cy >= m_create_btn_y + 56 && cy <= m_create_btn_y + 104) {
        m_show_banner = false; return;
    }

    if (m_dialog_open) {
        for (int i = 0; i < 5; ++i) {
            if (cx >= m_expiry_x[i] && cx <= m_expiry_x[i] + m_expiry_w[i] &&
                cy >= m_expiry_y[i] && cy <= m_expiry_y[i] + m_expiry_h[i]) {
                m_dlg_expiry_type = i;
                return;
            }
        }
        if (cx >= m_cancel_x && cx <= m_cancel_x + m_cancel_w &&
            cy >= m_cancel_y && cy <= m_cancel_y + m_cancel_h) {
            m_dialog_open = false;
            return;
        }
        if (cx >= m_confirm_x && cx <= m_confirm_x + m_confirm_w &&
            cy >= m_confirm_y && cy <= m_confirm_y + m_confirm_h) {
            if (m_create_cb) {
                std::string name = m_dlg_name.empty() ? "Desktop key" : m_dlg_name;
                m_last_new_key = m_create_cb(name, m_dlg_expiry_type, m_dlg_custom_ms);
                m_show_banner = !m_last_new_key.empty();
            }
            m_dialog_open = false;
            return;
        }
        bool inside = cx >= m_dialog_x && cx <= m_dialog_x + m_dialog_w &&
                      cy >= m_dialog_y && cy <= m_dialog_y + m_dialog_h;
        if (!inside) m_dialog_open = false;
        return;
    }

    // Table row actions
    for (const auto& r : m_row_rects) {
        if (cx < r.x0 || cx > r.x1) continue;
        if (cy < r.y0 || cy > r.y1) continue;
        if (r.is_delete && m_delete_cb) { m_delete_cb(r.key_id); return; }
        if (r.is_rotate && m_rotate_cb) {
            std::string newkey = m_rotate_cb(r.key_id);
            if (!newkey.empty()) { m_last_new_key = newkey; m_show_banner = true; }
            return;
        }
    }
}

void ApiPage::OnChar(wchar_t ch) {
    if (!m_dialog_open || !m_dlg_name_focus) return;
    if (ch == L'\b') {
        if (!m_dlg_name.empty()) m_dlg_name.pop_back();
    } else if (ch == L'\r') {
        // Create
        if (m_create_cb) {
            std::string name = m_dlg_name.empty() ? "Desktop key" : m_dlg_name;
            m_last_new_key = m_create_cb(name, m_dlg_expiry_type, m_dlg_custom_ms);
            m_show_banner = !m_last_new_key.empty();
        }
        m_dialog_open = false;
    } else if (ch == 27) {
        m_dialog_open = false;
    } else if (ch >= 32 && m_dlg_name.size() < 48) {
        m_dlg_name += (char)ch;
    }
}

} // namespace Client
