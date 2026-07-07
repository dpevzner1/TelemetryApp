#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dwrite.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace Client {

// Owns D3D11 device + swap chain + D2D device context.
// One instance per HWND. Call Resize() on WM_SIZE.
struct D2DContext {
    // D3D11
    ComPtr<ID3D11Device>        d3d_device;
    ComPtr<ID3D11DeviceContext> d3d_ctx;
    ComPtr<IDXGISwapChain1>     swap_chain;

    // D2D
    ComPtr<ID2D1Factory1>       d2d_factory;
    ComPtr<ID2D1Device>         d2d_device;
    ComPtr<ID2D1DeviceContext>  dc;

    // DirectWrite
    ComPtr<IDWriteFactory>      dw_factory;
    ComPtr<IDWriteTextFormat>   font_small;
    ComPtr<IDWriteTextFormat>   font_label;
    ComPtr<IDWriteTextFormat>   font_value;

    // Common brushes (created once; reuse across frames)
    ComPtr<ID2D1SolidColorBrush> br_bg;
    ComPtr<ID2D1SolidColorBrush> br_panel;
    ComPtr<ID2D1SolidColorBrush> br_text;
    ComPtr<ID2D1SolidColorBrush> br_accent;
    ComPtr<ID2D1SolidColorBrush> br_wave_cpu;
    ComPtr<ID2D1SolidColorBrush> br_wave_mem;
    ComPtr<ID2D1SolidColorBrush> br_wave_gpu;
    ComPtr<ID2D1SolidColorBrush> br_wave_disk;
    ComPtr<ID2D1SolidColorBrush> br_wave_net;
    ComPtr<ID2D1SolidColorBrush> br_grid;

    // Scratch brush for ad-hoc colors — SetColor before use
    ComPtr<ID2D1SolidColorBrush> br_scratch;

    UINT width  = 0;
    UINT height = 0;

    bool Init(HWND hwnd, UINT w, UINT h);
    bool Resize(UINT w, UINT h);
    void Destroy();

    void BeginDraw() { dc->BeginDraw(); }
    HRESULT EndDraw() { return dc->EndDraw(); }
    void Present() { swap_chain->Present(1, 0); }
    void Clear(D2D1_COLOR_F col) { dc->Clear(col); }

    // Helper accessors
    ID2D1DeviceContext* DC() const { return dc.Get(); }

    // Returns br_scratch with the given color set.
    // NOT thread-safe — only call from the render thread.
    ID2D1SolidColorBrush* BrushSolid(D2D1_COLOR_F col) {
        br_scratch->SetColor(col); return br_scratch.Get();
    }

    // Quick DWrite helper: draws text in a rect with the given color and point size.
    // bold=true uses font_label weight; false uses font_small weight.
    void DrawText(const wchar_t* text,
                  D2D1_RECT_F rect,
                  D2D1_COLOR_F color,
                  float size_pt     = 12.0f,
                  bool  bold        = false);

    // Creates a fresh text format (caller must Release)
    IDWriteTextFormat* MakeTextFormat(float size_pt, bool bold, bool center = false);
};

} // namespace Client
