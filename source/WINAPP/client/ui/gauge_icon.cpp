#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cmath>
#include <algorithm>
#include "gauge_icon.h"

namespace Client {

// ── GDI gauge drawing ─────────────────────────────────────────────────────────
// Draws a simple red circular barometer gauge onto an HDC of given size.
// The icon should read as "measurement" at both taskbar and title-bar sizes.

static void DrawGaugeOnDC(HDC hdc, int W) {
    int cx = W / 2, cy = W / 2;
    float r  = (float)(W / 2 - 1);

    // Red circular barometer body.
    HBRUSH bgBr = CreateSolidBrush(RGB(130, 18, 24));
    HPEN   bgPn = CreatePen(PS_SOLID, std::max(1, W / 18), RGB(255, 96, 96));
    SelectObject(hdc, bgBr);
    SelectObject(hdc, bgPn);
    Ellipse(hdc, 0, 0, W, W);
    DeleteObject(bgBr);
    DeleteObject(bgPn);

    // White tick scale over a red dial.
    const double PI = 3.14159265358979;
    auto toRad = [&](double deg) { return deg * PI / 180.0; };

    // Scale arc ticks: 9 major ticks (0..8), every 30°
    float arc_r = r * 0.75f;
    float tick_r = r * 0.85f;
    float start_deg = 225.0f; // 12 o'clock is 270, gauge starts bottom-left

    for (int i = 0; i <= 8; ++i) {
        double ang = toRad(start_deg - i * 33.75); // 270° / 8 = 33.75°/tick
        // Outer tick point
        float ox = (float)cx + tick_r * (float)cos(ang);
        float oy = (float)cy - tick_r * (float)sin(ang);
        // Inner tick point
        float inner = (i % 4 == 0) ? arc_r * 0.85f : arc_r * 0.95f;
        float ix = (float)cx + inner * (float)cos(ang);
        float iy = (float)cy - inner * (float)sin(ang);

        COLORREF col = (i % 4 == 0) ? RGB(255, 245, 245) : RGB(255, 180, 180);
        HPEN tick_pen = CreatePen(PS_SOLID, std::max(1, W / 16), col);
        SelectObject(hdc, tick_pen);
        MoveToEx(hdc, (int)ix, (int)iy, nullptr);
        LineTo(hdc, (int)ox, (int)oy);
        DeleteObject(tick_pen);
    }

    // Needle points into the high range, reinforcing active measurement.
    double needle_deg = start_deg - 0.7 * 270.0; // 70% of the sweep
    double nrad = toRad(needle_deg);
    float needle_len = r * 0.60f;
    float nx = (float)cx + needle_len * (float)cos(nrad);
    float ny = (float)cy - needle_len * (float)sin(nrad);
    float tail_len = r * 0.15f;
    float tx = (float)cx - tail_len * (float)cos(nrad);
    float ty = (float)cy + tail_len * (float)sin(nrad);

    HPEN needlePen = CreatePen(PS_SOLID, std::max(1, W / 11), RGB(255, 255, 255));
    SelectObject(hdc, needlePen);
    MoveToEx(hdc, (int)tx, (int)ty, nullptr);
    LineTo(hdc, (int)nx, (int)ny);
    DeleteObject(needlePen);

    // Hub dot.
    int hub_r = std::max(2, W / 10);
    HBRUSH hubBr = CreateSolidBrush(RGB(255, 235, 235));
    HPEN   hubPn = CreatePen(PS_SOLID, 1, RGB(90, 0, 0));
    SelectObject(hdc, hubBr);
    SelectObject(hdc, hubPn);
    Ellipse(hdc, cx - hub_r, cy - hub_r, cx + hub_r, cy + hub_r);
    DeleteObject(hubBr);
    DeleteObject(hubPn);

    // Outer rim.
    HPEN rimPen = CreatePen(PS_SOLID, std::max(1, W / 16), RGB(255, 58, 58));
    SelectObject(hdc, rimPen);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Ellipse(hdc, 1, 1, W - 1, W - 1);
    DeleteObject(rimPen);
}

// ── Create HICON ──────────────────────────────────────────────────────────────

HICON CreateGaugeIcon(int size) {
    HDC screen = GetDC(nullptr);
    HDC mem    = CreateCompatibleDC(screen);

    // 32-bpp DIBSection for ARGB support (alpha channel = opaque)
    BITMAPINFOHEADER bi{};
    bi.biSize        = sizeof(bi);
    bi.biWidth       = size;
    bi.biHeight      = -size; // top-down
    bi.biPlanes      = 1;
    bi.biBitCount    = 32;
    bi.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(mem, reinterpret_cast<BITMAPINFO*>(&bi),
                                   DIB_RGB_COLORS, &bits, nullptr, 0);
    HBITMAP old = (HBITMAP)SelectObject(mem, dib);

    // Start transparent; after GDI drawing, make non-background pixels opaque.
    DWORD* px = static_cast<DWORD*>(bits);
    for (int i = 0; i < size * size; ++i) px[i] = 0x00000000u;

    SetBkMode(mem, TRANSPARENT);
    DrawGaugeOnDC(mem, size);

    for (int i = 0; i < size * size; ++i) {
        if ((px[i] & 0x00FFFFFFu) != 0) px[i] |= 0xFF000000u;
    }

    SelectObject(mem, old);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);

    // Create mono mask (all zeros = colour comes from XOR mask entirely)
    HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);

    ICONINFO ii{};
    ii.fIcon    = TRUE;
    ii.hbmColor = dib;
    ii.hbmMask  = mask;
    HICON icon = CreateIconIndirect(&ii);

    DeleteObject(dib);
    DeleteObject(mask);
    return icon;
}

void SetGaugeIcon(HWND hwnd) {
    static HICON s_big  = nullptr;
    static HICON s_small = nullptr;
    if (!s_big)   s_big  = CreateGaugeIcon(32);
    if (!s_small) s_small = CreateGaugeIcon(16);
    if (s_big)   SendMessageW(hwnd, WM_SETICON, ICON_BIG,   (LPARAM)s_big);
    if (s_small) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)s_small);
}

} // namespace Client
