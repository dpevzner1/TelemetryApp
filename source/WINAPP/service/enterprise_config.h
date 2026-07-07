#pragma once
#include <string>
#include <cstdint>
#include <nlohmann/json.hpp>

namespace Service {

struct EnterpriseConfig {
    std::string data_dir;
    std::string sensor_id;
    std::string install_mode;
    std::string host_url;
    std::string enrollment_state;
    std::string enrolled_host_name;
    std::string enrolled_host_instance;
    std::string enrolled_host_address;
    int64_t     generated_at_ms = 0;
    int64_t     enrolled_at_ms = 0;
};

bool EnterpriseConfigInit(const std::string& data_dir);
const EnterpriseConfig& GetEnterpriseConfig();
nlohmann::json EnterpriseConfigJson(bool public_view);
nlohmann::json InstallAuditJson();
bool RecordLabEnrollment(const std::string& host_name,
                         const std::string& host_instance,
                         const std::string& host_address);

} // namespace Service
