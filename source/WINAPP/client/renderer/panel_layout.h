#pragma once
#include "d2d_context.h"
#include "waveform_graph.h"
#include "gauge_panel.h"
#include "../../shared/metric_ids.h"
#include <vector>
#include <cstdint>

namespace Client {

// 7-panel fixed layout:
//   [CPU waveform | MEM gauge | GPU waveform]
//   [DISK waveform | NET waveform | TEMP gauges | SELF gauge]
//
// Each panel occupies a proportional rect within the window.

class PanelLayout {
public:
    void Init(D2DContext* ctx, UINT window_w, UINT window_h);
    void Destroy();

    // Call on WM_SIZE
    void Resize(D2DContext* ctx, UINT w, UINT h);

    // Push new data from SHM read; waveforms will mark dirty
    struct FrameData {
        double cpu_pct;
        double mem_pct;
        double gpu_pct;        // primary GPU
        double disk_busy_pct;  // primary disk
        double net_recv_mbs;   // primary NIC recv Mb/s
        double temp_cpu_c;     // first temperature reading
        double self_cpu_pct;
        double vram_pct;
        double gpu_power_w;
        double gpu_temp_c;
        double mem_used_gb;
        double mem_total_gb;
        double freq_mhz;
        int    gpu_count;
        int    nic_count;
    };
    void PushData(const FrameData& d);

    // Called every render frame
    void DrawAll(D2DContext* ctx);

private:
    UINT m_w = 0, m_h = 0;

    WaveformGraph m_cpu_wave;
    WaveformGraph m_gpu_wave;
    WaveformGraph m_disk_wave;
    WaveformGraph m_net_wave;

    GaugePanel m_mem_gauge;
    GaugePanel m_gpu_power_gauge;
    GaugePanel m_temp_gauge;
    GaugePanel m_self_gauge;
    GaugePanel m_vram_gauge;

    struct PanelRect { float x, y, w, h; };
    PanelRect Layout(int panel_idx) const;
};

} // namespace Client
