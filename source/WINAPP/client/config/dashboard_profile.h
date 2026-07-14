#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace Client {

// Visualization type for a metric panel
enum class VizType : int {
    LineGraph   = 0,  // rolling time series
    ArcGauge    = 1,  // semicircle gauge with color zones
    BarGauge    = 2,  // horizontal fill bar
    Numeral     = 3,  // large number only
    NumeralTrend= 4,  // numeral + mini sparkline
    HeatMap     = 5,  // 2D grid (per-core CPU)
    DualLine    = 6,  // two overlaid lines (read/write)
    LedIndicator= 7,  // binary on/off dot
    Badge       = 8   // static text/badge
};

struct MetricPanel {
    uint32_t    metric_id;
    std::string label;
    std::string cluster;        // "cpu" | "memory" | "gpu" | "disk" | "network" | "temp" | "process" | "self"
    VizType     viz_type;
    bool        visible         = true;
    bool        logged          = true;
    std::string color;          // hex "#RRGGBB"
    int         grid_col        = 0;
    int         grid_row        = 0;
    int         grid_col_span   = 1;
    int         grid_row_span   = 1;
    float       y_min           = 0.0f;
    float       y_max           = 100.0f;
    std::string unit;           // "%", "MHz", "C", "W", "MB", "MB/s", "/s", ""
    int         history_samples = 300;     // how many samples to show in line graphs
};

struct DashboardProfile {
    std::string             name;
    std::string             created_iso;   // ISO 8601
    std::string             modified_iso;
    std::vector<MetricPanel> panels;

    // Logging config embedded in profile
    bool         logging_enabled  = false;
    int          log_interval_sec = 1;
    std::string  log_format       = "jsonl";

    bool Load(const std::string& path);
    bool Save(const std::string& path) const;
    bool SaveAs(const std::string& path) const { return Save(path); }

    // Build the default profile matching DevOps-recommended layout
    static DashboardProfile MakeDefault();

    std::string FilePath() const { return m_file_path; }
    void SetFilePath(const std::string& p) { m_file_path = p; }

private:
    std::string m_file_path;
};

// Returns viz type name for dropdown labels
const char* VizTypeName(VizType v);
// Returns allowed viz types for a given unit/cluster
std::vector<VizType> AllowedVizTypes(const std::string& cluster, const std::string& unit);

} // namespace Client
