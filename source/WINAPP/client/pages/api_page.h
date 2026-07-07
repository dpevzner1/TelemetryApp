#pragma once
#include "../renderer/d2d_context.h"
#include <string>
#include <vector>
#include <functional>

namespace Client {

// Mirrors Service::ApiKey fields that are safe to send to the client
struct ApiKeyInfo {
    std::string id;
    std::string name;
    std::string key_prefix;     // "tlm-XXXXXXXX..."
    int64_t     created_at_ms;
    int         expiry_type;    // mirrors Service::ExpiryType int
    int64_t     expires_at_ms;  // 0 = permanent/session
    bool        active;
    std::string status;         // "Active" / "Expired" / "Revoked"
};

class ApiPage {
public:
    explicit ApiPage(D2DContext& ctx);

    void SetKeys(std::vector<ApiKeyInfo> keys);
    const std::vector<ApiKeyInfo>& Keys() const { return m_keys; }

    // Callbacks wired by window
    using CreateCb  = std::function<std::string(const std::string& name, int expiry_type, int64_t custom_ms)>;
    using DeleteCb  = std::function<void(const std::string& id)>;
    using RotateCb  = std::function<std::string(const std::string& id)>;
    void SetCallbacks(CreateCb c, DeleteCb d, RotateCb r) {
        m_create_cb = c; m_delete_cb = d; m_rotate_cb = r;
    }

    void Draw(float x, float y, float w, float h, float dpi_scale);
    void OnClick(float x, float y);
    void OnChar(wchar_t ch);

    // Last newly created plaintext key (show-once dialog)
    std::string LastNewKey() const { return m_last_new_key; }
    void ClearLastNewKey() { m_last_new_key.clear(); }

private:
    D2DContext& m_ctx;
    std::vector<ApiKeyInfo> m_keys;

    CreateCb m_create_cb;
    DeleteCb m_delete_cb;
    RotateCb m_rotate_cb;

    // Create dialog state
    bool        m_dialog_open = false;
    std::string m_dlg_name;
    int         m_dlg_expiry_type = 0; // 0=Permanent
    int64_t     m_dlg_custom_ms   = 0;
    bool        m_dlg_name_focus  = false;
    int         m_dlg_date_field  = 0; // year/month/day cycling
    std::string m_last_new_key;

    // Row hit rects
    struct RowRect {
        float x0, x1;
        float y0, y1;
        std::string key_id;
        bool is_delete;
        bool is_rotate;
    };
    std::vector<RowRect> m_row_rects;

    // Show-once banner for new key
    bool   m_show_banner = false;

    // Button rects
    float m_create_btn_x = 0, m_create_btn_y = 0,
          m_create_btn_w = 0, m_create_btn_h = 0;
    float m_dialog_x = 0, m_dialog_y = 0, m_dialog_w = 0, m_dialog_h = 0;
    float m_cancel_x = 0, m_cancel_y = 0, m_cancel_w = 0, m_cancel_h = 0;
    float m_confirm_x = 0, m_confirm_y = 0, m_confirm_w = 0, m_confirm_h = 0;
    float m_expiry_x[5]{}, m_expiry_y[5]{}, m_expiry_w[5]{}, m_expiry_h[5]{};

    void DrawTable(float x, float y, float w, float h);
    void DrawRow(float x, float y, float w, const ApiKeyInfo& k);
    void DrawCreateDialog(float x, float y, float w, float h);
    void DrawNewKeyBanner(float x, float y, float w);

    static std::string FmtMs(int64_t ms);
    static const char* ExpiryLabel(int t);
};

} // namespace Client
