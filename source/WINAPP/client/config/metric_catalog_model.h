#pragma once
#include "dashboard_profile.h"
#include <cstdint>
#include <string>
#include <vector>

namespace Client {

struct MetricCatalogEntry {
    uint32_t    metric_id = 0;
    std::string label;
    std::string cluster;
    std::string unit;
    VizType     viz_type = VizType::LineGraph;
    bool        dashboard = true;
    bool        hud = false;
    bool        logging = true;
    bool        api = true;
    int         interval_sec = 1;
};

class MetricCatalogModel {
public:
    void BuildFromDashboard(const DashboardProfile& profile,
                            const std::vector<uint32_t>& hud_metric_ids);

    void ApplyToDashboard(DashboardProfile& profile) const;
    void UpdateLoggingSelection(const std::vector<uint32_t>& metric_ids);
    void UpdateHudSelection(const std::vector<uint32_t>& metric_ids);

    std::vector<uint32_t> LoggedMetricIds() const;
    std::vector<uint32_t> HudMetricIds() const;
    std::vector<uint32_t> DashboardMetricIds() const;

    bool Load(const std::string& path);
    bool Save(const std::string& path) const;

    const std::vector<MetricCatalogEntry>& Entries() const { return m_entries; }

private:
    std::vector<MetricCatalogEntry> m_entries;

    MetricCatalogEntry* Find(uint32_t metric_id);
    const MetricCatalogEntry* Find(uint32_t metric_id) const;
};

} // namespace Client
