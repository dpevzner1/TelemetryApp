#include "dashboard_profile.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <string>

using json = nlohmann::json;
using namespace std::chrono;

namespace Client {

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::string IsoNow() {
    auto t = system_clock::to_time_t(system_clock::now());
    struct tm tm_val{};
    gmtime_s(&tm_val, &t);
    char buf[32]{};
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_val);
    return buf;
}

static void ReplaceAll(std::string& s, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

static std::string CleanUiText(std::string s) {
    ReplaceAll(s, "\xC2\xB0""C", "C");
    ReplaceAll(s, "\xC3\x82\xC2\xB0""C", "C");
    ReplaceAll(s, "Â°C", "C");
    ReplaceAll(s, "°C", "C");
    ReplaceAll(s, "├C", "C");
    ReplaceAll(s, "┬C", "C");
    ReplaceAll(s, "ƒC", "C");
    ReplaceAll(s, "\xEF\xBF\xBD", "");
    return s;
}

const char* VizTypeName(VizType v) {
    switch (v) {
    case VizType::LineGraph:    return "Line Graph";
    case VizType::ArcGauge:    return "Arc Gauge";
    case VizType::BarGauge:    return "Bar Gauge";
    case VizType::Numeral:     return "Numeral";
    case VizType::NumeralTrend:return "Numeral + Trend";
    case VizType::HeatMap:     return "Heat Map";
    case VizType::DualLine:    return "Dual Line";
    case VizType::LedIndicator:return "LED Indicator";
    case VizType::Badge:       return "Badge";
    default:                   return "Unknown";
    }
}

std::vector<VizType> AllowedVizTypes(const std::string& cluster, const std::string& unit) {
    // Utility % metrics
    if (unit == "%")
        return {VizType::LineGraph, VizType::ArcGauge, VizType::BarGauge,
                VizType::Numeral, VizType::NumeralTrend};
    // Temperature
    if (unit == "C")
        return {VizType::ArcGauge, VizType::LineGraph, VizType::Numeral, VizType::NumeralTrend};
    // Rate metrics
    if (unit == "MB/s" || unit == "/s" || unit == "IOPS")
        return {VizType::LineGraph, VizType::DualLine, VizType::Numeral, VizType::NumeralTrend};
    // Power
    if (unit == "W")
        return {VizType::LineGraph, VizType::ArcGauge, VizType::Numeral, VizType::NumeralTrend};
    // Frequency
    if (unit == "MHz")
        return {VizType::NumeralTrend, VizType::Numeral, VizType::LineGraph};
    // Binary
    if (unit == "bool")
        return {VizType::LedIndicator, VizType::Badge};
    // Per-core
    if (cluster == "cpu_cores")
        return {VizType::HeatMap, VizType::LineGraph};
    // Absolute values (GB, MB)
    return {VizType::BarGauge, VizType::Numeral, VizType::NumeralTrend, VizType::LineGraph};
}

// ── Default profile (DevOps-recommended layout) ───────────────────────────────
// metric_ids match the MetricId enum in shared/metric_ids.h
DashboardProfile DashboardProfile::MakeDefault() {
    DashboardProfile p;
    p.name         = "Default";
    p.created_iso  = IsoNow();
    p.modified_iso = p.created_iso;

    int row = 0, col = 0;
    auto add = [&](uint32_t id, const char* label, const char* cluster,
                   VizType viz, const char* color,
                   float ymax, const char* unit,
                   int cspan = 1, int rspan = 1) {
        MetricPanel mp;
        mp.metric_id    = id;
        mp.label        = label;
        mp.cluster      = cluster;
        mp.viz_type     = viz;
        mp.color        = color;
        mp.y_max        = ymax;
        mp.unit         = unit;
        mp.grid_col     = col;
        mp.grid_row     = row;
        mp.grid_col_span= cspan;
        mp.grid_row_span= rspan;
        mp.visible      = true;
        mp.logged       = true;
        p.panels.push_back(mp);
        col += cspan;
        if (col >= 12) { col = 0; ++row; }
    };

    // ── CPU cluster (green) ───────────────────────────────────────────────────
    // Row 0: big waveform + freq + balance
    add(0,  "CPU Total",       "cpu", VizType::LineGraph,    "#4DCC66", 100, "%",   4);
    add(1,  "Core Freq",       "cpu", VizType::NumeralTrend, "#6BE08A", 6000,"MHz", 2);
    add(3,  "Balance Score",   "cpu", VizType::ArcGauge,     "#4DCC66", 1.0f,"",   2);
    add(4,  "Hot Core",        "cpu", VizType::Numeral,       "#FFD700", 64,  "#",  2);
    add(5,  "Hot Core %",      "cpu", VizType::NumeralTrend,  "#FFD700", 100, "%",  2);
    // Row 1: per-core heat map (16 cols = all 12 cols)
    add(16, "Per-Core Usage",  "cpu_cores", VizType::HeatMap, "#4DCC66", 100, "%", 12);

    // ── Memory cluster (orange) ───────────────────────────────────────────────
    add(67, "RAM Used %",      "memory", VizType::ArcGauge,    "#F2A61A", 100,  "%",  3);
    add(65, "RAM Used (GB)",   "memory", VizType::BarGauge,     "#F2A61A", 128,  "GB", 3);
    add(66, "Available (GB)",  "memory", VizType::Numeral,      "#F5C761", 128,  "GB", 2);
    add(71, "Standby (GB)",    "memory", VizType::NumeralTrend, "#A8D8A8", 128,  "GB", 2);
    add(75, "Page Faults/s",   "memory", VizType::LineGraph,    "#F27A1A", 5000, "/s", 2);
    add(69, "Swap Used (GB)",  "memory", VizType::BarGauge,     "#D4820A", 64,   "GB", 2);
    add(73, "Commit Total GB", "memory", VizType::NumeralTrend, "#BBBBBB", 256,  "GB", 2);

    // ── GPU cluster (blue) — GPU 0 = metrics 96-127 ───────────────────────────
    add(96, "GPU Usage %",     "gpu", VizType::LineGraph,    "#40A0FF", 100,  "%",  4);
    add(99, "GPU Temp C",      "gpu", VizType::ArcGauge,     "#FF6060", 100,  "C",  2);
    add(100,"GPU Power W",     "gpu", VizType::LineGraph,     "#FFB84D", 300,  "W",  3);
    add(98, "VRAM %",          "gpu", VizType::ArcGauge,      "#40A0FF", 100,  "%",  3);
    add(97, "VRAM Used MB",    "gpu", VizType::BarGauge,      "#2080E0", 12288,"MB", 3);
    add(108,"Encoder %",       "gpu", VizType::Numeral,       "#7BBFFF", 100,  "%",  2);
    add(109,"Decoder %",       "gpu", VizType::Numeral,       "#7BBFFF", 100,  "%",  2);
    add(102,"Core Clock MHz",  "gpu", VizType::NumeralTrend,  "#B0D4FF", 3000, "MHz",2);
    add(103,"Mem Clock MHz",   "gpu", VizType::NumeralTrend,  "#B0D4FF", 10000,"MHz",2);
    add(104,"Throttle",        "gpu", VizType::LedIndicator,  "#FF3030", 1,    "bool",2);
    add(110,"GPU SM Util %",   "gpu", VizType::LineGraph,     "#60BFFF", 100,  "%",  4);
    add(111,"Mem BW Util %",   "gpu", VizType::LineGraph,     "#60BFFF", 100,  "%",  4);

    // ── Disk cluster (purple) ─────────────────────────────────────────────────
    add(224,"Disk Read MB/s",  "disk", VizType::DualLine,    "#D966E5", 2000, "MB/s",4);
    add(225,"Disk Write MB/s", "disk", VizType::DualLine,    "#B340CC", 2000, "MB/s",4);
    add(228,"Disk Busy %",     "disk", VizType::BarGauge,    "#D966E5", 100,  "%",   2);
    add(226,"Read IOPS",       "disk", VizType::NumeralTrend,"#CC99DD", 100000,"IOPS",2);
    add(227,"Write IOPS",      "disk", VizType::NumeralTrend,"#CC99DD", 100000,"IOPS",2);

    // ── Network cluster (cyan) ────────────────────────────────────────────────
    add(288,"Net Recv MB/s",   "network", VizType::DualLine,    "#66D9E5", 1000, "MB/s",4);
    add(289,"Net Send MB/s",   "network", VizType::DualLine,    "#40B8C6", 1000, "MB/s",4);
    add(290,"Recv Pkts/s",     "network", VizType::NumeralTrend,"#99E8F0", 100000,"/s",  2);
    add(291,"Send Pkts/s",     "network", VizType::NumeralTrend,"#99E8F0", 100000,"/s",  2);
    add(292,"Errors In",       "network", VizType::LedIndicator,"#FF4444", 1,    "bool", 2);

    // ── Temperatures cluster (red-orange) ─────────────────────────────────────
    add(352,"Thermal Zone 0",  "temp", VizType::ArcGauge,   "#FF6633", 100, "C",   3);
    add(353,"Thermal Zone 1",  "temp", VizType::ArcGauge,   "#FF8844", 100, "C",   3);
    add(354,"Thermal Zone 2",  "temp", VizType::ArcGauge,   "#FFAA55", 100, "C",   3);
    add(355,"Thermal Zone 3",  "temp", VizType::ArcGauge,   "#FFCC66", 100, "C",   3);

    // ── Self-monitoring (grey) ────────────────────────────────────────────────
    add(384,"Service CPU %",   "self", VizType::NumeralTrend,"#888888", 5,  "%",   3);
    add(388,"Poll Duration ms","self", VizType::NumeralTrend,"#888888", 200,"ms",  3);
    add(385,"Service RAM MB",  "self", VizType::Numeral,     "#888888", 100,"MB",  3);

    p.logging_enabled  = false;
    p.log_interval_sec = 1;
    p.log_format       = "jsonl";
    return p;
}

// ── Load / Save ───────────────────────────────────────────────────────────────

bool DashboardProfile::Load(const std::string& path) {
    m_file_path = path;
    std::ifstream f(path);
    if (!f.is_open()) return false;
    try {
        json j = json::parse(f);
        name          = j.value("name",          "Unnamed");
        created_iso   = j.value("created_iso",   "");
        modified_iso  = j.value("modified_iso",  "");
        logging_enabled   = j.value("logging_enabled",  false);
        log_interval_sec  = j.value("log_interval_sec", 1);
        log_format        = j.value("log_format",       "jsonl");
        panels.clear();
        for (auto& item : j["panels"]) {
            MetricPanel mp;
            mp.metric_id     = item["metric_id"];
            mp.label         = CleanUiText(item.value("label", ""));
            mp.cluster       = CleanUiText(item.value("cluster", ""));
            mp.viz_type      = static_cast<VizType>(item["viz_type"].get<int>());
            mp.visible       = item.value("visible",   true);
            mp.logged        = item.value("logged",    true);
            mp.color         = item.value("color",     "#FFFFFF");
            mp.grid_col      = item.value("grid_col",  0);
            mp.grid_row      = item.value("grid_row",  0);
            mp.grid_col_span = item.value("grid_col_span", 1);
            mp.grid_row_span = item.value("grid_row_span", 1);
            mp.y_min         = item.value("y_min",     0.0f);
            mp.y_max         = item.value("y_max",     100.0f);
            mp.unit          = CleanUiText(item.value("unit",      "%"));
            mp.history_samples= item.value("history_samples", 300);
            panels.push_back(mp);
        }
    } catch (...) { return false; }
    return true;
}

bool DashboardProfile::Save(const std::string& path) const {
    json j;
    j["schema_version"]   = 1;
    j["name"]             = name;
    j["created_iso"]      = created_iso;
    j["modified_iso"]     = IsoNow();
    j["logging_enabled"]  = logging_enabled;
    j["log_interval_sec"] = log_interval_sec;
    j["log_format"]       = log_format;
    j["panels"]           = json::array();
    for (const auto& mp : panels) {
        j["panels"].push_back({
            {"metric_id",      mp.metric_id},
            {"label",          mp.label},
            {"cluster",        mp.cluster},
            {"viz_type",       static_cast<int>(mp.viz_type)},
            {"visible",        mp.visible},
            {"logged",         mp.logged},
            {"color",          mp.color},
            {"grid_col",       mp.grid_col},
            {"grid_row",       mp.grid_row},
            {"grid_col_span",  mp.grid_col_span},
            {"grid_row_span",  mp.grid_row_span},
            {"y_min",          mp.y_min},
            {"y_max",          mp.y_max},
            {"unit",           mp.unit},
            {"history_samples",mp.history_samples}
        });
    }
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << j.dump(2);
    return true;
}

} // namespace Client
