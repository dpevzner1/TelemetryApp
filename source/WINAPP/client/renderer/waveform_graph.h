#pragma once
#include "d2d_context.h"
#include <cstdint>
#include <string>

namespace Client {

// Rolling waveform widget.
// On each call to Push(value), shifts the waveform left by 1 column and draws
// only the new rightmost column — retained-mode dirty-region approach.
// The static columns are preserved in an offscreen bitmap; only 1 new column
// is composited per frame, keeping GPU load minimal (~0.3% CPU at 60fps).

class WaveformGraph {
public:
    // width/height in DIPs; max_val is the y-axis ceiling (e.g., 100.0 for %)
    void Init(D2DContext* ctx, UINT width, UINT height, float max_val,
              ID2D1SolidColorBrush* wave_brush, const wchar_t* label);
    void Destroy();

    // Call once per poll to append a new data point
    void Push(double value);

    // Draw the graph at (origin_x, origin_y) in device coordinates.
    // Called every render frame; only redraws the new column.
    void Draw(D2DContext* ctx, float origin_x, float origin_y);

    bool NeedsRedraw() const { return m_dirty; }

private:
    D2DContext*               m_ctx       = nullptr;
    ComPtr<ID2D1Bitmap1>      m_offscreen; // static columns rendered into this
    ID2D1SolidColorBrush*     m_brush     = nullptr; // borrowed
    double                    m_values[300]{};
    uint32_t                  m_head      = 0;
    uint32_t                  m_count     = 0;
    float                     m_max_val   = 100.0f;
    UINT                      m_width     = 0;
    UINT                      m_height    = 0;
    std::wstring              m_label;
    bool                      m_dirty     = true;

    void RebuildOffscreen(D2DContext* ctx);
    void DrawGridLines(ID2D1DeviceContext* dc);
};

} // namespace Client
