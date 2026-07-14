#pragma once

#include <string>

namespace Sensors {

struct ReadingQuality {
    double value = 0.0;
    std::string unit;
    std::string source;
    std::string quality;
    std::string confidence;
    std::string reason;
};

struct PlatformPowerSnapshot {
    ReadingQuality ac_power_state;
    ReadingQuality battery_percent;
    ReadingQuality battery_rate_w;
    ReadingQuality platform_power_w;
};

PlatformPowerSnapshot QueryPlatformPower();

} // namespace Sensors
