#include "waveform_graph.h"
#include <d2d1_1helper.h>
#include <cstring>
#include <algorithm>
#include <cmath>

namespace Client {

static constexpr uint32_t GRID_LINES = 4; // horizontal guide lines at 25/50/75/100%

void WaveformGraph::Init(D2DContext* ctx, UINT width, UINT height, float max_val,
                         ID2D1SolidColorBrush* wave_brush, const wchar_t* label)
{
    m_ctx     = ctx;
    m_width   = width;
    m_height  = height;
    m_max_val = max_val;
    m_brush   = wave_brush;
    m_label   = label ? label : L"";
    m_head    = 0;
    m_count   = 0;
    m_dirty   = true;
    std::memset(m_values, 0, sizeof(m_values));

    // Create the offscreen bitmap (same pixel format as swap chain)
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ctx->dc->CreateBitmap(D2D1::SizeU(width, height), nullptr, 0, bp, &m_offscreen);
}

void WaveformGraph::Destroy() {
    m_offscreen.Reset();
}

void WaveformGraph::Push(double value) {
    m_values[m_head] = value;
    m_head = (m_head + 1) % 300;
    if (m_count < 300) ++m_count;
    m_dirty = true;
}

void WaveformGraph::DrawGridLines(ID2D1DeviceContext* dc) {
    for (uint32_t i = 1; i <= GRID_LINES; ++i) {
        float y = m_height * (1.0f - i / static_cast<float>(GRID_LINES + 1));
        dc->DrawLine(D2D1::Point2F(0, y), D2D1::Point2F(static_cast<float>(m_width), y),
                     m_ctx->br_grid.Get(), 0.5f);
    }
}

void WaveformGraph::RebuildOffscreen(D2DContext* ctx) {
    if (!m_offscreen) return;
    ID2D1Image* prev_target = nullptr;
    ctx->dc->GetTarget(&prev_target);
    ctx->dc->SetTarget(m_offscreen.Get());
    ctx->dc->BeginDraw();
    ctx->dc->Clear(D2D1::ColorF(0.08f, 0.08f, 0.10f, 1.0f));
    DrawGridLines(ctx->dc.Get());

    float col_w = static_cast<float>(m_width) / 300.0f;
    for (uint32_t i = 0; i < m_count; ++i) {
        uint32_t data_idx = (m_head - m_count + i + 300) % 300;
        double   v        = std::max(0.0, std::min(static_cast<double>(m_max_val), m_values[data_idx]));
        float    bar_h    = static_cast<float>(v / m_max_val) * m_height;
        float    x0       = i * col_w;
        float    x1       = x0 + col_w;
        float    y0       = m_height - bar_h;
        ctx->dc->FillRectangle(D2D1::RectF(x0, y0, x1, static_cast<float>(m_height)),
                               m_brush);
    }
    ctx->dc->EndDraw();
    ctx->dc->SetTarget(prev_target);
    if (prev_target) prev_target->Release();
    m_dirty = false;
}

void WaveformGraph::Draw(D2DContext* ctx, float origin_x, float origin_y) {
    if (m_dirty) RebuildOffscreen(ctx);
    if (!m_offscreen) return;

    // Draw the offscreen bitmap at the target origin
    ctx->dc->DrawBitmap(m_offscreen.Get(),
        D2D1::RectF(origin_x, origin_y,
                    origin_x + m_width,
                    origin_y + m_height));

    // Overlay label (top-left corner)
    if (!m_label.empty()) {
        D2D1_RECT_F lbl_rect = D2D1::RectF(origin_x + 4, origin_y + 2,
                                            origin_x + m_width - 4, origin_y + 18);
        ctx->dc->DrawTextW(m_label.c_str(), static_cast<UINT32>(m_label.size()),
                           ctx->font_small.Get(), lbl_rect, ctx->br_text.Get());
    }
}

} // namespace Client
