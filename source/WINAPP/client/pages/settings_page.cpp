#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include "settings_page.h"
#include "../../shared/app_version.h"
#include <d2d1_1helper.h>
#include <cstring>
#include <cstdio>

namespace Client {

static constexpr D2D1_COLOR_F kBg     = {0.10f, 0.10f, 0.12f, 1.0f};
static constexpr D2D1_COLOR_F kPanel  = {0.14f, 0.14f, 0.18f, 1.0f};
static constexpr D2D1_COLOR_F kText   = {0.88f, 0.88f, 0.90f, 1.0f};
static constexpr D2D1_COLOR_F kDim    = {0.50f, 0.50f, 0.54f, 1.0f};
static constexpr D2D1_COLOR_F kGreen  = {0.30f, 0.80f, 0.40f, 1.0f};
static constexpr D2D1_COLOR_F kRed    = {0.85f, 0.25f, 0.25f, 1.0f};
static constexpr D2D1_COLOR_F kBlue   = {0.25f, 0.60f, 1.00f, 1.0f};
static constexpr D2D1_COLOR_F kBorder = {0.22f, 0.22f, 0.26f, 1.0f};

SettingsPage::SettingsPage(D2DContext& ctx) : m_ctx(ctx) {}

static std::wstring WidenAscii(const char* s) {
    std::wstring out;
    if (!s) return out;
    while (*s) out.push_back(static_cast<unsigned char>(*s++));
    return out;
}

void SettingsPage::DrawSection(float x, float& y, float w, const wchar_t* title) {
    m_ctx.DC()->DrawLine({x + 20, y + 8}, {x + w - 20, y + 8},
                          m_ctx.BrushSolid(kBorder), 0.5f);
    m_ctx.DrawText(title, {x + 20, y, x + w - 20, y + 20}, kDim, 10.0f, true);
    y += 24;
}

void SettingsPage::DrawField(float x, float& y, float w, const wchar_t* label,
                              std::string& val, int field_id) {
    m_ctx.DrawText(label, {x + 20, y, x + 160, y + 16}, kDim, 10.0f);
    D2D1_RECT_F inp = {x + 170, y - 2, x + w - 20, y + 20};
    bool focused = m_focused_field == field_id;
    m_ctx.DC()->FillRoundedRectangle(D2D1::RoundedRect(inp, 4, 4),
        m_ctx.BrushSolid({0.08f, 0.08f, 0.10f, 1.0f}));
    m_ctx.DC()->DrawRoundedRectangle(D2D1::RoundedRect(inp, 4, 4),
        m_ctx.BrushSolid(focused ? kBlue : kBorder), 1.0f);
    std::wstring wv(val.begin(), val.end());
    if (focused) wv += L"|";
    m_ctx.DrawText(wv.c_str(), {inp.left + 6, y, inp.right - 4, y + 18}, kText, 11.0f);
    m_btns.push_back({inp.left, inp.top, inp.right, inp.bottom, 100 + field_id});
    y += 28;
}

void SettingsPage::DrawToggle(float x, float& y, float w, const wchar_t* label, bool val, int btn_id) {
    m_ctx.DrawText(label, {x + 20, y, x + 240, y + 16}, kText, 11.0f);
    // Toggle pill
    float tx = x + w - 70, ty = y - 2;
    D2D1_RECT_F pill = {tx, ty, tx + 52, ty + 20};
    D2D1_COLOR_F pc = val ? D2D1_COLOR_F{0.12f,0.30f,0.15f,1} : D2D1_COLOR_F{0.15f,0.15f,0.18f,1};
    m_ctx.DC()->FillRoundedRectangle(D2D1::RoundedRect(pill, 10, 10), m_ctx.BrushSolid(pc));
    D2D1_ELLIPSE dot = {{val ? tx + 40 : tx + 12, ty + 10}, 8, 8};
    m_ctx.DC()->FillEllipse(dot, m_ctx.BrushSolid(val ? kGreen : kDim));
    m_ctx.DrawText(val ? L"ON" : L"OFF", pill, val ? kGreen : kDim, 9.0f, true);
    m_btns.push_back({tx, ty, tx + 52, ty + 20, btn_id});
    y += 28;
}

void SettingsPage::DrawButton(float x, float y, float bw, float bh,
                               const wchar_t* label, D2D1_COLOR_F col, int btn_id) {
    D2D1_RECT_F r = {x, y, x + bw, y + bh};
    m_ctx.DC()->FillRoundedRectangle(D2D1::RoundedRect(r, 5, 5), m_ctx.BrushSolid(col));
    m_ctx.DrawText(label, r, {1,1,1,1}, 11.0f, true);
    m_btns.push_back({x, y, x + bw, y + bh, btn_id});
}

void SettingsPage::DrawStatusBadge(float x, float y, float /*w*/) {
    auto* dc = m_ctx.DC();
    D2D1_COLOR_F col = service_connected ? kGreen : kRed;
    D2D1_ELLIPSE dot = {{x + 10, y + 10}, 6, 6};
    dc->FillEllipse(dot, m_ctx.BrushSolid(col));
    const wchar_t* status = service_connected ? L"Service Connected" : L"Service Disconnected";
    m_ctx.DrawText(status, {x + 22, y, x + 220, y + 22}, col, 11.0f);

    if (service_connected) {
        char buf[64]{};
        snprintf(buf, sizeof(buf), "  %d active key(s)   poll %.1f ms", active_keys, poll_duration_ms);
        std::wstring ws(buf, buf + strlen(buf));
        m_ctx.DrawText(ws.c_str(), {x + 220, y, x + 560, y + 22}, kDim, 10.0f);
    }
}

void SettingsPage::Draw(float x, float y, float w, float h, float /*dpi*/) {
    m_ctx.DC()->FillRectangle({x, y, x + w, y + h}, m_ctx.BrushSolid(kBg));
    m_btns.clear();

    // Sync editable copies from config on first draw
    if (m_cfg && m_edit_api_url.empty()) {
        m_edit_api_url = m_cfg->api_url;
        m_edit_api_key = m_cfg->api_key;
        char pb[8]{}; snprintf(pb, sizeof(pb), "%d", m_cfg->poll_interval_ms);
        m_edit_poll_ms = pb;
    }

    float cy = y + 16;

    m_ctx.DrawText(L"Settings", {x + 20, cy, x + w - 20, cy + 36}, kText, 20.0f, true);
    cy += 44;

    // Status row
    DrawStatusBadge(x, cy, w);
    cy += 32;

    // ── Service Connection ─────────────────────────────────────────────────────
    DrawSection(x, cy, w, L"SERVICE CONNECTION");
    if (m_cfg) {
        DrawField(x, cy, w, L"API URL:",  m_edit_api_url, 0);
        DrawField(x, cy, w, L"API Key:",  m_edit_api_key, 1);
        DrawField(x, cy, w, L"Poll (ms):",m_edit_poll_ms, 2);
    }
    cy += 4;

    // ── Display ───────────────────────────────────────────────────────────────
    DrawSection(x, cy, w, L"DISPLAY");
    if (m_cfg) {
        DrawToggle(x, cy, w, L"Start minimized", m_cfg->start_minimized, BTN_START_MINIMIZED);
        DrawToggle(x, cy, w, L"Start with service", m_cfg->start_with_service, BTN_START_SERVICE);
    }
    cy += 4;

    // ── Dashboard Profiles ────────────────────────────────────────────────────
    DrawSection(x, cy, w, L"DASHBOARD PROFILES");
    m_ctx.DrawText(L"Active profile:", {x + 20, cy, x + 140, cy + 16}, kDim, 10.0f);
    if (m_cfg) {
        std::wstring wp(m_cfg->active_dashboard_profile.begin(),
                        m_cfg->active_dashboard_profile.end());
        m_ctx.DrawText(wp.c_str(), {x + 150, cy, x + w - 200, cy + 16}, kText, 11.0f, true);
    }
    cy += 24;
    DrawButton(x + 20, cy, 140, 28, L"Export Profile", {0.14f,0.20f,0.34f,1}, BTN_EXPORT_PROFILE);
    DrawButton(x + 170, cy, 140, 28, L"Import Profile", {0.14f,0.20f,0.34f,1}, BTN_IMPORT_PROFILE);
    cy += 40;

    // ── HUD Mode ──────────────────────────────────────────────────────────────
    DrawSection(x, cy, w, L"HUD / MINIMIZE BEHAVIOR");
    if (m_cfg) {
        // Minimize behavior radio group
        m_ctx.DrawText(L"When minimized:", {x + 20, cy, x + 160, cy + 16}, kDim, 10.0f);
        cy += 20;

        struct { const wchar_t* label; int btn_id; MinimizeBehavior val; } min_opts[] = {
            {L"Normal minimize",  BTN_MIN_NORMAL, MinimizeBehavior::Normal},
            {L"HUD bar (default)", BTN_MIN_HUD,   MinimizeBehavior::HUDMode},
            {L"Hide to tray", BTN_MIN_TRAY,   MinimizeBehavior::SystemTray},
        };
        float bx = x + 20;
        for (auto& opt : min_opts) {
            bool active = (m_cfg->minimize_behavior == opt.val);
            D2D1_COLOR_F bc = active
                ? D2D1_COLOR_F{0.15f, 0.35f, 0.60f, 1.0f}
                : D2D1_COLOR_F{0.14f, 0.14f, 0.18f, 1.0f};
            DrawButton(bx, cy, 150, 26, opt.label, bc, opt.btn_id);
            bx += 158;
        }
        cy += 38;

        // HUD position selector
        m_ctx.DrawText(L"HUD bar position:", {x + 20, cy, x + 160, cy + 16}, kDim, 10.0f);
        cy += 20;

        struct { const wchar_t* label; int btn_id; HudPositionCfg val; } pos_opts[] = {
            {L"Above taskbar",  BTN_HUD_POS_ABOVE, HudPositionCfg::AboveTaskbar},
            {L"Top of screen",  BTN_HUD_POS_TOP,   HudPositionCfg::Top},
            {L"Left edge",      BTN_HUD_POS_LEFT,  HudPositionCfg::Left},
            {L"Right edge",     BTN_HUD_POS_RIGHT, HudPositionCfg::Right},
        };
        bx = x + 20;
        for (auto& opt : pos_opts) {
            bool active = (m_cfg->hud_position == opt.val);
            D2D1_COLOR_F bc = active
                ? D2D1_COLOR_F{0.12f, 0.30f, 0.12f, 1.0f}
                : D2D1_COLOR_F{0.14f, 0.14f, 0.18f, 1.0f};
            DrawButton(bx, cy, 140, 26, opt.label, bc, opt.btn_id);
            bx += 148;
        }
        cy += 42;
    }

    // ── Service Management ────────────────────────────────────────────────────
    DrawSection(x, cy, w, L"SERVICE MANAGEMENT");
    m_ctx.DrawText(L"Run installer to install/uninstall as Windows Service.",
        {x + 20, cy, x + w - 20, cy + 16}, kDim, 10.0f);
    cy += 20;
    DrawButton(x + 20, cy, 160, 28, L"Restart Service",
               {0.20f, 0.20f, 0.25f, 1}, BTN_RESTART_SVC);
    DrawButton(x + 190, cy, 140, 28, L"Open Data Dir",
               {0.14f, 0.20f, 0.34f, 1}, BTN_OPEN_DATA_DIR);
    cy += 40;

    DrawSection(x, cy, w, L"ABOUT");
    std::wstring about = L"TelemetryApp v" + WidenAscii(TelemetryApp::APP_VERSION) +
        L" - native Windows telemetry monitor for live dashboards, HUD, API capture, Prometheus, SSE, and JSONL process-run logs.";
    m_ctx.DrawText(about.c_str(),
        {x + 20, cy, x + w - 20, cy + 18}, kText, 10.0f);
    cy += 18;
    m_ctx.DrawText(L"Created by Demit Pevzner - MIT License - https://github.com/dpevzner1/TelemetryApp - demitri.pevzner@gmail.com",
        {x + 20, cy, x + w - 20, cy + 18}, kDim, 10.0f);
    cy += 30;

    // ── Actions ──────────────────────────────────────────────────────────────
    DrawButton(x + w - 130, cy, 110, 32, L"Save Settings",
               {0.15f, 0.35f, 0.60f, 1}, BTN_SAVE);
    cy += 44;

    // App version footer
    std::wstring footer = L"TelemetryApp v" + WidenAscii(TelemetryApp::APP_VERSION) +
        L" - MIT License - Demit Pevzner 2026 - github.com/dpevzner1/TelemetryApp";
    m_ctx.DrawText(footer.c_str(),
        {x + 20, y + h - 28, x + w - 20, y + h - 8}, kDim, 9.0f);
}

// ── Interaction ───────────────────────────────────────────────────────────────

void SettingsPage::OnClick(float cx, float cy) {
    for (const auto& b : m_btns) {
        if (cx < b.x0 || cx > b.x1 || cy < b.y0 || cy > b.y1) continue;

        if (b.id >= 100 && b.id < 200) {
            // Field focus
            m_focused_field = b.id - 100;
            return;
        }
        HandleButton(b.id);
        m_focused_field = -1;
        return;
    }
    m_focused_field = -1;
}

void SettingsPage::OnChar(wchar_t ch) {
    if (m_focused_field < 0) return;
    std::string* target = nullptr;
    switch (m_focused_field) {
    case 0: target = &m_edit_api_url; break;
    case 1: target = &m_edit_api_key; break;
    case 2: target = &m_edit_poll_ms; break;
    }
    if (!target) return;
    if (ch == L'\b') { if (!target->empty()) target->pop_back(); }
    else if (ch == L'\r') { CommitEdits(); m_focused_field = -1; }
    else if (ch == 27)    { m_focused_field = -1; }
    else if (ch >= 32 && target->size() < 256) *target += (char)ch;
}

void SettingsPage::CommitEdits() {
    if (!m_cfg) return;
    if (!m_edit_api_url.empty()) m_cfg->api_url = m_edit_api_url;
    if (!m_edit_api_key.empty()) m_cfg->api_key = m_edit_api_key;
    int ms = atoi(m_edit_poll_ms.c_str());
    if (ms >= 100 && ms <= 60000) m_cfg->poll_interval_ms = ms;
}

void SettingsPage::HandleButton(int btn_id) {
    switch (btn_id) {
    case BTN_SAVE:
        CommitEdits();
        if (m_on_save) m_on_save();
        break;
    case BTN_RESTART_SVC:
        // Kick the service — fire-and-forget
        ShellExecuteW(nullptr, L"runas", L"sc", L"stop TelemetryService", nullptr, SW_HIDE);
        Sleep(1500);
        ShellExecuteW(nullptr, L"runas", L"sc", L"start TelemetryService", nullptr, SW_HIDE);
        break;
    case BTN_OPEN_DATA_DIR:
        if (m_cfg && !m_cfg->data_dir.empty()) {
            std::wstring wd(m_cfg->data_dir.begin(), m_cfg->data_dir.end());
            ShellExecuteW(nullptr, L"open", L"explorer.exe", wd.c_str(), nullptr, SW_SHOWNORMAL);
        }
        break;
    case BTN_EXPORT_PROFILE:
    case BTN_IMPORT_PROFILE:
        if (m_on_save) m_on_save();
        break;
    // HUD minimize behavior
    case BTN_MIN_NORMAL: if (m_cfg) m_cfg->minimize_behavior = MinimizeBehavior::Normal;   break;
    case BTN_MIN_HUD:    if (m_cfg) m_cfg->minimize_behavior = MinimizeBehavior::HUDMode;  break;
    case BTN_MIN_TRAY:   if (m_cfg) m_cfg->minimize_behavior = MinimizeBehavior::SystemTray; break;
    // HUD position
    case BTN_HUD_POS_ABOVE: if (m_cfg) m_cfg->hud_position = HudPositionCfg::AboveTaskbar; break;
    case BTN_HUD_POS_TOP:   if (m_cfg) m_cfg->hud_position = HudPositionCfg::Top;          break;
    case BTN_HUD_POS_LEFT:  if (m_cfg) m_cfg->hud_position = HudPositionCfg::Left;         break;
    case BTN_HUD_POS_RIGHT: if (m_cfg) m_cfg->hud_position = HudPositionCfg::Right;        break;
    case BTN_START_MINIMIZED:
        if (m_cfg) {
            m_cfg->start_minimized = !m_cfg->start_minimized;
            if (m_on_save) m_on_save();
        }
        break;
    case BTN_START_SERVICE:
        if (m_cfg) {
            m_cfg->start_with_service = !m_cfg->start_with_service;
            if (m_on_save) m_on_save();
        }
        break;
    default: break;
    }
}

} // namespace Client

