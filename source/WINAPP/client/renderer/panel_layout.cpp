#include "panel_layout.h"
#include <d2d1_1helper.h>
#include <cmath>

namespace Client {

void PanelLayout::Init(D2DContext* ctx, UINT w, UINT h) {
    m_w = w; m_h = h;

    // Waveforms — allocate at panel size; Resize() will recreate if needed
    auto pr = [&](int i) { return Layout(i); };

    m_cpu_wave.Init(ctx,  static_cast<UINT>(pr(0).w), static_cast<UINT>(pr(0).h),
                   100.0f, ctx->br_wave_cpu.Get(),  L"CPU %");
    m_gpu_wave.Init(ctx,  static_cast<UINT>(pr(2).w), static_cast<UINT>(pr(2).h),
                   100.0f, ctx->br_wave_gpu.Get(),  L"GPU %");
    m_disk_wave.Init(ctx, static_cast<UINT>(pr(3).w), static_cast<UINT>(pr(3).h),
                   100.0f, ctx->br_wave_disk.Get(), L"Disk busy %");
    m_net_wave.Init(ctx,  static_cast<UINT>(pr(4).w), static_cast<UINT>(pr(4).h),
                   100.0f, ctx->br_wave_net.Get(),  L"Net recv MB/s");

    // Gauges (layout-only; values set in PushData)
    m_mem_gauge      = {L"Memory",     L"%",   0, 0, 100, 0, true, ctx->br_wave_mem.Get()};
    m_vram_gauge     = {L"VRAM",       L"%",   0, 0, 100, 0, true, ctx->br_wave_gpu.Get()};
    m_gpu_power_gauge= {L"GPU Power",  L"W",   0, 0, 300, 0, true, ctx->br_wave_gpu.Get()};
    m_temp_gauge     = {L"CPU Temp",   L"C",   0, 0, 100, 0, true, ctx->br_wave_cpu.Get()};
    m_self_gauge     = {L"Self CPU",   L"%",   0, 0,  10, 1, true, ctx->br_accent.Get()};
}

void PanelLayout::Destroy() {
    m_cpu_wave.Destroy();
    m_gpu_wave.Destroy();
    m_disk_wave.Destroy();
    m_net_wave.Destroy();
}

void PanelLayout::Resize(D2DContext* ctx, UINT w, UINT h) {
    m_w = w; m_h = h;
    // Recreate waveform offscreen bitmaps at new size
    m_cpu_wave.Destroy();  m_gpu_wave.Destroy();
    m_disk_wave.Destroy(); m_net_wave.Destroy();
    Init(ctx, w, h);
}

// Fixed 7-panel layout:
//   Row 0 (top, 55%): [0=CPU wave 35%] [1=MEM gauge 15%] [2=GPU wave 35%] [3=VRAM gauge 15%]
//   Row 1 (bot, 45%): [4=DISK wave 25%] [5=NET wave 25%] [6=TEMP 15%] [7=PWR 15%] [8=SELF 20%]
PanelLayout::PanelRect PanelLayout::Layout(int panel_idx) const {
    float W = static_cast<float>(m_w);
    float H = static_cast<float>(m_h);
    float row0_h = H * 0.55f;
    float row1_h = H - row0_h;
    float margin = 4.0f;

    switch (panel_idx) {
    case 0: return {margin,           margin,          W*0.35f - margin*1.5f, row0_h - margin*2};
    case 1: return {W*0.35f+margin/2, margin,          W*0.15f - margin,      row0_h - margin*2};
    case 2: return {W*0.50f+margin/2, margin,          W*0.35f - margin,      row0_h - margin*2};
    case 3: return {W*0.85f+margin/2, margin,          W*0.15f - margin*1.5f, row0_h - margin*2};
    case 4: return {margin,           row0_h+margin,   W*0.25f - margin*1.5f, row1_h - margin*2};
    case 5: return {W*0.25f+margin/2, row0_h+margin,   W*0.25f - margin,      row1_h - margin*2};
    case 6: return {W*0.50f+margin/2, row0_h+margin,   W*0.15f - margin,      row1_h - margin*2};
    case 7: return {W*0.65f+margin/2, row0_h+margin,   W*0.15f - margin,      row1_h - margin*2};
    case 8: return {W*0.80f+margin/2, row0_h+margin,   W*0.20f - margin*1.5f, row1_h - margin*2};
    default: return {0,0,0,0};
    }
}

void PanelLayout::PushData(const FrameData& d) {
    m_cpu_wave.Push(d.cpu_pct);
    m_gpu_wave.Push(d.gpu_pct);
    m_disk_wave.Push(d.disk_busy_pct);
    m_net_wave.Push(d.net_recv_mbs);

    m_mem_gauge.value       = d.mem_pct;
    m_vram_gauge.value      = d.vram_pct;
    m_gpu_power_gauge.value = d.gpu_power_w;
    m_temp_gauge.value      = d.gpu_temp_c > 0 ? d.gpu_temp_c : d.temp_cpu_c;
    m_self_gauge.value      = d.self_cpu_pct;
}

void PanelLayout::DrawAll(D2DContext* ctx) {
    ctx->dc->Clear(D2D1::ColorF(0.08f, 0.08f, 0.10f, 1.0f));

    auto P = [&](int i) { return Layout(i); };

    // Waveforms
    auto p0 = P(0); m_cpu_wave.Draw(ctx,  p0.x, p0.y);
    auto p2 = P(2); m_gpu_wave.Draw(ctx,  p2.x, p2.y);
    auto p4 = P(4); m_disk_wave.Draw(ctx, p4.x, p4.y);
    auto p5 = P(5); m_net_wave.Draw(ctx,  p5.x, p5.y);

    // Gauges
    auto p1 = P(1); m_mem_gauge.Draw(ctx,       p1.x, p1.y, p1.w, p1.h);
    auto p3 = P(3); m_vram_gauge.Draw(ctx,       p3.x, p3.y, p3.w, p3.h);
    auto p6 = P(6); m_temp_gauge.Draw(ctx,       p6.x, p6.y, p6.w, p6.h);
    auto p7 = P(7); m_gpu_power_gauge.Draw(ctx,  p7.x, p7.y, p7.w, p7.h);
    auto p8 = P(8); m_self_gauge.Draw(ctx,       p8.x, p8.y, p8.w, p8.h);
}

} // namespace Client
