#include "d2d_context.h"
#include <d2d1_1helper.h>
#include <dxgi1_2.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dwmapi.lib")

namespace Client {

static const D2D1_COLOR_F kBg      = {0.08f, 0.08f, 0.10f, 1.0f};
static const D2D1_COLOR_F kPanel   = {0.12f, 0.12f, 0.15f, 1.0f};
static const D2D1_COLOR_F kText    = {0.90f, 0.90f, 0.92f, 1.0f};
static const D2D1_COLOR_F kAccent  = {0.25f, 0.65f, 1.00f, 1.0f};
static const D2D1_COLOR_F kCpu     = {0.30f, 0.80f, 0.40f, 1.0f};
static const D2D1_COLOR_F kMem     = {0.95f, 0.65f, 0.10f, 1.0f};
static const D2D1_COLOR_F kGpu     = {0.25f, 0.60f, 1.00f, 1.0f};
static const D2D1_COLOR_F kDisk    = {0.85f, 0.40f, 0.90f, 1.0f};
static const D2D1_COLOR_F kNet     = {0.40f, 0.85f, 0.90f, 1.0f};
static const D2D1_COLOR_F kGrid    = {0.20f, 0.20f, 0.24f, 1.0f};

bool D2DContext::Init(HWND hwnd, UINT w, UINT h) {
    width = w; height = h;

    // D3D11 device + DXGI swap chain
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   flags, &fl, 1, D3D11_SDK_VERSION,
                                   &d3d_device, nullptr, &d3d_ctx);
    if (FAILED(hr)) return false;

    ComPtr<IDXGIFactory2> dxgi_factory;
    ComPtr<IDXGIDevice>   dxgi_dev;
    d3d_device.As(&dxgi_dev);
    ComPtr<IDXGIAdapter> adapter;
    dxgi_dev->GetAdapter(&adapter);
    adapter->GetParent(IID_PPV_ARGS(&dxgi_factory));

    DXGI_SWAP_CHAIN_DESC1 scd{};
    scd.Width  = w; scd.Height = h;
    scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.SampleDesc.Count = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 2;
    scd.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    hr = dxgi_factory->CreateSwapChainForHwnd(d3d_device.Get(), hwnd, &scd,
                                               nullptr, nullptr, &swap_chain);
    if (FAILED(hr)) return false;

    // D2D factory + device + device context
    D2D1_FACTORY_OPTIONS opts{};
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, opts, d2d_factory.GetAddressOf());
    if (FAILED(hr)) return false;

    ComPtr<ID2D1Factory1> f1;
    d2d_factory.As(&f1);
    f1->CreateDevice(dxgi_dev.Get(), &d2d_device);
    d2d_device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc);

    // Bind swap chain back buffer as D2D render target
    ComPtr<IDXGISurface> surface;
    swap_chain->GetBuffer(0, IID_PPV_ARGS(&surface));
    ComPtr<ID2D1Bitmap1> bmp;
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
    dc->CreateBitmapFromDxgiSurface(surface.Get(), bp, &bmp);
    dc->SetTarget(bmp.Get());

    // DirectWrite
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                        reinterpret_cast<IUnknown**>(dw_factory.GetAddressOf()));
    dw_factory->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        11.0f, L"en-us", &font_small);
    dw_factory->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        13.0f, L"en-us", &font_label);
    dw_factory->CreateTextFormat(L"Segoe UI", nullptr,
        DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        28.0f, L"en-us", &font_value);

    // Brushes
    auto MkBrush = [&](const D2D1_COLOR_F& c, ComPtr<ID2D1SolidColorBrush>& br) {
        dc->CreateSolidColorBrush(c, &br);
    };
    MkBrush(kBg,     br_bg);
    MkBrush(kPanel,  br_panel);
    MkBrush(kText,   br_text);
    MkBrush(kAccent, br_accent);
    MkBrush(kCpu,    br_wave_cpu);
    MkBrush(kMem,    br_wave_mem);
    MkBrush(kGpu,    br_wave_gpu);
    MkBrush(kDisk,   br_wave_disk);
    MkBrush(kNet,    br_wave_net);
    MkBrush(kGrid,   br_grid);
    MkBrush(kBg,     br_scratch);  // scratch: color set dynamically per call

    return true;
}

bool D2DContext::Resize(UINT w, UINT h) {
    if (w == width && h == height) return true;
    width = w; height = h;
    dc->SetTarget(nullptr);
    swap_chain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
    ComPtr<IDXGISurface> surface;
    swap_chain->GetBuffer(0, IID_PPV_ARGS(&surface));
    ComPtr<ID2D1Bitmap1> bmp;
    D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
    HRESULT hr = dc->CreateBitmapFromDxgiSurface(surface.Get(), bp, &bmp);
    if (FAILED(hr)) return false;
    dc->SetTarget(bmp.Get());
    return true;
}

void D2DContext::Destroy() {
    br_scratch.Reset();
    br_wave_net.Reset(); br_wave_disk.Reset(); br_wave_gpu.Reset();
    br_wave_mem.Reset(); br_wave_cpu.Reset();
    br_grid.Reset(); br_accent.Reset(); br_text.Reset();
    br_panel.Reset(); br_bg.Reset();
    font_value.Reset(); font_label.Reset(); font_small.Reset();
    dw_factory.Reset();
    dc.Reset(); d2d_device.Reset(); d2d_factory.Reset();
    swap_chain.Reset(); d3d_ctx.Reset(); d3d_device.Reset();
}

IDWriteTextFormat* D2DContext::MakeTextFormat(float size_pt, bool bold, bool center) {
    IDWriteTextFormat* fmt = nullptr;
    dw_factory->CreateTextFormat(
        L"Segoe UI", nullptr,
        bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
        size_pt, L"en-us", &fmt);
    if (fmt && center) {
        fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    return fmt;
}

void D2DContext::DrawText(const wchar_t* text, D2D1_RECT_F rect,
                          D2D1_COLOR_F color, float size_pt, bool bold) {
    if (!text || !*text) return;
    IDWriteTextFormat* fmt = MakeTextFormat(size_pt, bold);
    if (!fmt) return;
    br_scratch->SetColor(color);
    dc->DrawText(text, (UINT32)wcslen(text), fmt, rect, br_scratch.Get(),
                 D2D1_DRAW_TEXT_OPTIONS_CLIP);
    fmt->Release();
}

} // namespace Client
