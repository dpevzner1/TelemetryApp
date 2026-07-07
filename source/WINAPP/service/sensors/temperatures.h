#pragma once
#include <string>
#include <vector>

namespace Sensors {

struct TempReading {
    std::string name;
    std::string component;
    double celsius;
};

bool TempInit();
bool TempPoll(std::vector<TempReading>& readings);
void TempShutdown();

} // namespace Sensors
