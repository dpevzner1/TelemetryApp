#pragma once

#include <nlohmann/json.hpp>

struct ShmBlock;

namespace Service {

// Builds an additive hardware identity and capability view. This does not
// mutate collector state or the v1 shared-memory layout.
nlohmann::json BuildHardwareInventoryJson(const ShmBlock* shm);

} // namespace Service
