#include "metric_catalog_model.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <unordered_set>

using json = nlohmann::json;

namespace Client {

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

static int DefaultIntervalForCluster(const std::string& cluster) {
    if (cluster == "temp" || cluster == "memory") return 10;
    if (cluster == "process" || cluster == "self") return 1;
    return 1;
}

MetricCatalogEntry* MetricCatalogModel::Find(uint32_t metric_id) {
    for (auto& e : m_entries) {
        if (e.metric_id == metric_id) return &e;
    }
    return nullptr;
}

const MetricCatalogEntry* MetricCatalogModel::Find(uint32_t metric_id) const {
    for (const auto& e : m_entries) {
        if (e.metric_id == metric_id) return &e;
    }
    return nullptr;
}

void MetricCatalogModel::BuildFromDashboard(const DashboardProfile& profile,
                                            const std::vector<uint32_t>& hud_metric_ids) {
    std::unordered_set<uint32_t> hud(hud_metric_ids.begin(), hud_metric_ids.end());
    std::vector<MetricCatalogEntry> rebuilt;
    rebuilt.reserve(profile.panels.size());

    for (const auto& panel : profile.panels) {
        MetricCatalogEntry entry;
        entry.metric_id = panel.metric_id;
        entry.label = CleanUiText(panel.label);
        entry.cluster = CleanUiText(panel.cluster);
        entry.unit = CleanUiText(panel.unit);
        entry.viz_type = panel.viz_type;
        entry.dashboard = panel.visible;
        entry.hud = hud.count(panel.metric_id) != 0;
        entry.logging = panel.logged;
        entry.api = true;
        entry.interval_sec = DefaultIntervalForCluster(panel.cluster);

        if (const auto* existing = Find(panel.metric_id)) {
            entry.hud = existing->hud;
            entry.logging = existing->logging;
            entry.api = existing->api;
            entry.interval_sec = existing->interval_sec;
        }
        rebuilt.push_back(entry);
    }

    m_entries = std::move(rebuilt);
}

void MetricCatalogModel::ApplyToDashboard(DashboardProfile& profile) const {
    for (auto& panel : profile.panels) {
        if (const auto* entry = Find(panel.metric_id)) {
            panel.visible = entry->dashboard;
            panel.logged = entry->logging;
            panel.viz_type = entry->viz_type;
        }
    }
}

void MetricCatalogModel::UpdateLoggingSelection(const std::vector<uint32_t>& metric_ids) {
    std::unordered_set<uint32_t> selected(metric_ids.begin(), metric_ids.end());
    for (auto& entry : m_entries) {
        entry.logging = selected.count(entry.metric_id) != 0;
    }
}

void MetricCatalogModel::UpdateHudSelection(const std::vector<uint32_t>& metric_ids) {
    std::unordered_set<uint32_t> selected(metric_ids.begin(), metric_ids.end());
    for (auto& entry : m_entries) {
        entry.hud = selected.count(entry.metric_id) != 0;
    }
}

std::vector<uint32_t> MetricCatalogModel::LoggedMetricIds() const {
    std::vector<uint32_t> ids;
    for (const auto& entry : m_entries) {
        if (entry.logging) ids.push_back(entry.metric_id);
    }
    return ids;
}

std::vector<uint32_t> MetricCatalogModel::HudMetricIds() const {
    std::vector<uint32_t> ids;
    for (const auto& entry : m_entries) {
        if (entry.hud) ids.push_back(entry.metric_id);
    }
    return ids;
}

std::vector<uint32_t> MetricCatalogModel::DashboardMetricIds() const {
    std::vector<uint32_t> ids;
    for (const auto& entry : m_entries) {
        if (entry.dashboard) ids.push_back(entry.metric_id);
    }
    return ids;
}

bool MetricCatalogModel::Load(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    try {
        json j = json::parse(f);
        m_entries.clear();
        for (const auto& item : j.value("entries", json::array())) {
            MetricCatalogEntry entry;
            entry.metric_id = item.value("metric_id", 0u);
            entry.label = CleanUiText(item.value("label", ""));
            entry.cluster = CleanUiText(item.value("cluster", ""));
            entry.unit = CleanUiText(item.value("unit", ""));
            entry.viz_type = static_cast<VizType>(item.value("viz_type", 0));
            entry.dashboard = item.value("dashboard", true);
            entry.hud = item.value("hud", false);
            entry.logging = item.value("logging", true);
            entry.api = item.value("api", true);
            entry.interval_sec = item.value("interval_sec", DefaultIntervalForCluster(entry.cluster));
            m_entries.push_back(entry);
        }
    } catch (...) {
        return false;
    }
    return true;
}

bool MetricCatalogModel::Save(const std::string& path) const {
    json entries = json::array();
    for (const auto& entry : m_entries) {
        entries.push_back({
            {"metric_id", entry.metric_id},
            {"label", entry.label},
            {"cluster", entry.cluster},
            {"unit", entry.unit},
            {"viz_type", static_cast<int>(entry.viz_type)},
            {"dashboard", entry.dashboard},
            {"hud", entry.hud},
            {"logging", entry.logging},
            {"api", entry.api},
            {"interval_sec", entry.interval_sec}
        });
    }

    json j;
    j["schema"] = "telemetryapp.metric_catalog.v1";
    j["active_profile"] = "Balanced Default";
    j["entries"] = entries;

    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << j.dump(2);
    return true;
}

} // namespace Client
