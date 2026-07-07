#include "gauge_panel.h"
#include <d2d1_1helper.h>
#include <algorithm>
#include <cstdio>

namespace Client {

void GaugePanel::Draw(D2DContext* ctx, float x, float y, float w, float h) const {
    // Panel background
    ctx->dc->FillRectangle(D2D1::RectF(x, y, x+w, y+h), ctx->br_panel.Get());

    // Fill bar (bottom portion representing % of range)
    if (show_bar && bar_brush && max_val > min_val) {
        float frac = static_cast<float>((value - min_val) / (max_val - min_val));
        frac = std::max(0.0f, std::min(1.0f, frac));
        float bar_h = h * frac;
        ctx->dc->FillRectangle(
            D2D1::RectF(x, y + h - bar_h, x + w, y + h),
            bar_brush);
    }

    // Value text (large, centered)
    wchar_t val_str[32];
    swprintf_s(val_str, decimals == 0 ? L"%.0f" : (decimals == 1 ? L"%.1f" : L"%.2f"), value);
    std::wstring display = std::wstring(val_str) + L" " + unit;
    D2D1_RECT_F val_rect = D2D1::RectF(x + 4, y + h * 0.25f, x + w - 4, y + h * 0.70f);
    ctx->dc->DrawTextW(display.c_str(), static_cast<UINT32>(display.size()),
                       ctx->font_value.Get(), val_rect, ctx->br_text.Get());

    // Label (bottom)
    D2D1_RECT_F lbl_rect = D2D1::RectF(x + 4, y + h * 0.75f, x + w - 4, y + h - 2);
    ctx->dc->DrawTextW(label.c_str(), static_cast<UINT32>(label.size()),
                       ctx->font_label.Get(), lbl_rect, ctx->br_text.Get());

    // Border
    ctx->dc->DrawRectangle(D2D1::RectF(x, y, x+w, y+h), ctx->br_grid.Get(), 1.0f);
}

} // namespace Client
