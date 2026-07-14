#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <powerbase.h>
#include "power.h"

namespace Sensors {

namespace {

ReadingQuality MakeReading(double value,
                           const char* unit,
                           const char* source,
                           const char* quality,
                           const char* confidence,
                           const char* reason) {
    ReadingQuality r;
    r.value = value;
    r.unit = unit ? unit : "";
    r.source = source ? source : "";
    r.quality = quality ? quality : "";
    r.confidence = confidence ? confidence : "";
    r.reason = reason ? reason : "";
    return r;
}

} // namespace

PlatformPowerSnapshot QueryPlatformPower() {
    PlatformPowerSnapshot out;

    SYSTEM_POWER_STATUS ps{};
    if (GetSystemPowerStatus(&ps)) {
        const char* ac = "unknown";
        if (ps.ACLineStatus == 0) ac = "battery";
        else if (ps.ACLineStatus == 1) ac = "ac";
        out.ac_power_state = MakeReading(
            static_cast<double>(ps.ACLineStatus), "state", "Windows SYSTEM_POWER_STATUS",
            ps.ACLineStatus == 255 ? "unavailable" : "measured",
            ps.ACLineStatus == 255 ? "none" : "high", ac);

        if (ps.BatteryLifePercent <= 100) {
            out.battery_percent = MakeReading(
                static_cast<double>(ps.BatteryLifePercent), "%",
                "Windows SYSTEM_POWER_STATUS", "measured", "high", "");
        } else {
            out.battery_percent = MakeReading(
                0.0, "%", "Windows SYSTEM_POWER_STATUS", "unavailable", "none",
                "No battery percentage reported");
        }
    } else {
        out.ac_power_state = MakeReading(
            0.0, "state", "Windows SYSTEM_POWER_STATUS", "unavailable", "none",
            "GetSystemPowerStatus failed");
        out.battery_percent = MakeReading(
            0.0, "%", "Windows SYSTEM_POWER_STATUS", "unavailable", "none",
            "GetSystemPowerStatus failed");
    }

    SYSTEM_BATTERY_STATE bs{};
    LONG status = CallNtPowerInformation(SystemBatteryState, nullptr, 0, &bs, sizeof(bs));
    if (status == 0 && bs.BatteryPresent) {
        long rate_mw = static_cast<long>(bs.Rate);
        if (rate_mw == static_cast<long>(0x80000000)) {
            out.battery_rate_w = MakeReading(
                0.0, "W", "CallNtPowerInformation(SystemBatteryState)",
                "unavailable", "none", "Battery rate returned invalid sentinel");
            out.platform_power_w = MakeReading(
                0.0, "W", "Battery discharge estimate", "unavailable", "none",
                "Battery rate returned invalid sentinel");
        } else if (rate_mw < 0 && bs.Discharging) {
            double watts = static_cast<double>(-rate_mw) / 1000.0;
            out.battery_rate_w = MakeReading(
                watts, "W", "CallNtPowerInformation(SystemBatteryState)",
                "measured", "medium", "Battery discharge rate; not wall power");
            out.platform_power_w = MakeReading(
                watts, "W", "Battery discharge estimate",
                "derived", "medium",
                "Whole-device battery discharge estimate; excludes charger/AC conversion losses and is not wall power");
        } else if (rate_mw > 0 && bs.Charging) {
            double watts = static_cast<double>(rate_mw) / 1000.0;
            out.battery_rate_w = MakeReading(
                watts, "W", "CallNtPowerInformation(SystemBatteryState)",
                "measured", "medium", "Battery charge rate; not device consumption");
            out.platform_power_w = MakeReading(
                0.0, "W", "Battery discharge estimate", "unavailable", "none",
                "Battery is charging or AC-powered; discharge-based platform consumption is unavailable");
        } else {
            out.battery_rate_w = MakeReading(
                0.0, "W", "CallNtPowerInformation(SystemBatteryState)",
                "unavailable", "none", "Battery rate is zero or not currently discharging");
            out.platform_power_w = MakeReading(
                0.0, "W", "Battery discharge estimate", "unavailable", "none",
                "Battery is not discharging; use external meter/PDU/BMC for wall power");
        }
    } else {
        out.battery_rate_w = MakeReading(
            0.0, "W", "CallNtPowerInformation(SystemBatteryState)",
            "unavailable", "none", "No system battery state reported");
        out.platform_power_w = MakeReading(
            0.0, "W", "Battery discharge estimate", "unavailable", "none",
            "No battery discharge telemetry; whole-device wall power requires external meter/PDU/BMC");
    }

    return out;
}

} // namespace Sensors
