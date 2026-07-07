#pragma once
#include "../renderer/d2d_context.h"
#include "../config/app_config.h"
#include <string>
#include <vector>
#include <functional>

namespace Client {

class SettingsPage {
public:
    explicit SettingsPage(D2DContext& ctx);

    // Bind to the live config
    void SetConfig(AppConfig* cfg) { m_cfg = cfg; }

    // Called when user clicks Save — persists config and triggers service reconnect if URL changed
    using OnSaveCb = std::function<void()>;
    void SetOnSave(OnSaveCb cb) { m_on_save = std::move(cb); }

    // Service connection status (updated each tick)
    bool   service_connected    = false;
    int    active_keys          = 0;
    double poll_duration_ms     = 0.0;
    int    uptime_seconds       = 0;

    // Dashboard profile list (for switching active profile)
    void SetProfileNames(std::vector<std::string> names) { m_profiles = std::move(names); }

    void Draw(float x, float y, float w, float h, float dpi_scale);
    void OnClick(float x, float y);
    void OnChar(wchar_t ch);

private:
    D2DContext& m_ctx;
    AppConfig*  m_cfg = nullptr;
    OnSaveCb    m_on_save;

    std::vector<std::string> m_profiles;

    // Editable fields (transient, committed on Save)
    std::string m_edit_api_url;
    std::string m_edit_api_key;
    std::string m_edit_poll_ms;
    int         m_focused_field = -1; // 0=url, 1=key, 2=poll

    // Button hit rects
    struct BtnRect { float x0,y0,x1,y1; int id; };
    std::vector<BtnRect> m_btns;

    static constexpr int BTN_SAVE              = 1;
    static constexpr int BTN_RESTART_SVC       = 2;
    static constexpr int BTN_INSTALL_SVC       = 3;
    static constexpr int BTN_OPEN_DATA_DIR     = 4;
    static constexpr int BTN_EXPORT_PROFILE    = 5;
    static constexpr int BTN_IMPORT_PROFILE    = 6;
    // HUD — minimize behavior
    static constexpr int BTN_MIN_NORMAL        = 7;
    static constexpr int BTN_MIN_HUD           = 8;
    static constexpr int BTN_MIN_TRAY          = 9;
    // HUD — position
    static constexpr int BTN_HUD_POS_ABOVE     = 10;
    static constexpr int BTN_HUD_POS_TOP       = 11;
    static constexpr int BTN_HUD_POS_LEFT      = 12;
    static constexpr int BTN_HUD_POS_RIGHT     = 13;
    static constexpr int BTN_START_MINIMIZED   = 14;
    static constexpr int BTN_START_SERVICE     = 15;

    void DrawSection(float x, float& y, float w, const wchar_t* title);
    void DrawField(float x, float& y, float w, const wchar_t* label,
                   std::string& val, int field_id);
    void DrawToggle(float x, float& y, float w, const wchar_t* label, bool val, int btn_id);
    void DrawButton(float x, float y, float bw, float bh,
                    const wchar_t* label, D2D1_COLOR_F col, int btn_id);
    void DrawStatusBadge(float x, float y, float w);

    void CommitEdits();
    void HandleButton(int btn_id);
};

} // namespace Client
