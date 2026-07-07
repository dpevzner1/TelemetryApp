#pragma once
#include "d2d_context.h"
#include <string>

namespace Client {

// Draws a single numeric gauge: large value + unit + label + optional sub-values
struct GaugePanel {
    std::wstring label;
    std::wstring unit;
    double       value     = 0.0;
    double       min_val   = 0.0;
    double       max_val   = 100.0;
    int          decimals  = 1;
    bool         show_bar  = true;   // fill bar behind value
    ID2D1SolidColorBrush* bar_brush = nullptr; // borrowed

    void Draw(D2DContext* ctx, float x, float y, float w, float h) const;
};

} // namespace Client
