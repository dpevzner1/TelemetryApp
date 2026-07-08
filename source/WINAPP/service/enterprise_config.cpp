#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <objbase.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <vector>
#include "enterprise_config.h"
#include "diagnostic_log.h"

using json = nlohmann::json;

namespace Service {

static EnterpriseConfig s_cfg;

static int64_t NowMs() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u{ ft.dwLowDateTime, ft.dwHighDateTime };
    return static_cast<int64_t>((u.QuadPart - 116444736000000000ULL) / 10000ULL);
}

static void EnsureDir(const std::string& path) {
    std::string cur;
    for (char c : path) {
        cur += c;
        if (c == '\\' || c == '/') CreateDirectoryA(cur.c_str(), nullptr);
    }
    CreateDirectoryA(path.c_str(), nullptr);
}

static std::string Env(const char* name) {
    char buf[1024]{};
    size_t sz = 0;
    if (getenv_s(&sz, buf, sizeof(buf), name) == 0 && sz > 1)
        return std::string(buf, sz - 1);
    return {};
}

static std::string ReadRegStr(const char* value) {
    HKEY hk{};
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\TelemetryApp", 0,
                      KEY_READ | KEY_WOW64_64KEY, &hk) != ERROR_SUCCESS)
        return {};
    char buf[1024]{};
    DWORD type = 0;
    DWORD len = sizeof(buf);
    std::string out;
    if (RegQueryValueExA(hk, value, nullptr, &type,
                         reinterpret_cast<LPBYTE>(buf), &len) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ) && len > 1) {
        out.assign(buf, strnlen(buf, sizeof(buf)));
    }
    RegCloseKey(hk);
    return out;
}

static std::string NewGuidString() {
    GUID g{};
    if (CoCreateGuid(&g) != S_OK) {
        std::ostringstream fallback;
        fallback << "tlm-node-" << GetTickCount64();
        return fallback.str();
    }
    wchar_t wbuf[64]{};
    StringFromGUID2(g, wbuf, 64);
    char buf[96]{};
    WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, buf, sizeof(buf), nullptr, nullptr);
    std::string s = buf;
    s.erase(std::remove(s.begin(), s.end(), '{'), s.end());
    s.erase(std::remove(s.begin(), s.end(), '}'), s.end());
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return "tlm-node-" + s;
}

static std::string ShortPublicHash(const std::string& s) {
    // FNV-1a is used only as a short non-secret display fingerprint.
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    std::ostringstream os;
    os << std::hex << std::setw(12) << std::setfill('0') << (h & 0xFFFFFFFFFFFFULL);
    return os.str();
}

static std::string LocalHostname() {
    char name[MAX_COMPUTERNAME_LENGTH + 1]{};
    DWORD len = sizeof(name);
    if (GetComputerNameA(name, &len) && name[0]) return name;
    return "Windows Device";
}

static std::string PrimaryMacHash() {
    ULONG size = 0;
    if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                             nullptr, nullptr, &size) != ERROR_BUFFER_OVERFLOW || size == 0) {
        return {};
    }
    std::vector<unsigned char> buffer(size);
    auto* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    if (GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                             nullptr, adapters, &size) != NO_ERROR) {
        return {};
    }
    for (auto* a = adapters; a; a = a->Next) {
        if (a->PhysicalAddressLength == 0) continue;
        if (a->IfType != IF_TYPE_ETHERNET_CSMACD && a->IfType != IF_TYPE_IEEE80211) continue;
        if (a->OperStatus != IfOperStatusUp) continue;
        std::ostringstream raw;
        for (ULONG i = 0; i < a->PhysicalAddressLength; ++i) {
            raw << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(a->PhysicalAddress[i]);
        }
        return ShortPublicHash(raw.str());
    }
    return {};
}

static std::string ConfigPath(const std::string& data_dir) {
    return data_dir + "\\sensor\\enrollment.json";
}

static void SaveConfig() {
    EnsureDir(s_cfg.data_dir + "\\sensor");
    json j;
    j["sensor_id"] = s_cfg.sensor_id;
    j["install_mode"] = s_cfg.install_mode;
    j["host_url"] = s_cfg.host_url;
    j["enrollment_state"] = s_cfg.enrollment_state;
    j["enrolled_host_name"] = s_cfg.enrolled_host_name;
    j["enrolled_host_instance"] = s_cfg.enrolled_host_instance;
    j["enrolled_host_address"] = s_cfg.enrolled_host_address;
    j["generated_at_ms"] = s_cfg.generated_at_ms;
    j["enrolled_at_ms"] = s_cfg.enrolled_at_ms;
    std::ofstream f(ConfigPath(s_cfg.data_dir), std::ios::trunc);
    if (f.is_open()) f << j.dump(2);
}

bool EnterpriseConfigInit(const std::string& data_dir) {
    s_cfg = {};
    s_cfg.data_dir = data_dir;
    s_cfg.install_mode = Env("TELEMETRY_INSTALL_MODE");
    if (s_cfg.install_mode.empty()) s_cfg.install_mode = ReadRegStr("InstallMode");
    if (s_cfg.install_mode.empty()) s_cfg.install_mode = "FullHost";

    s_cfg.host_url = Env("TELEMETRY_HOST_URL");
    if (s_cfg.host_url.empty()) s_cfg.host_url = ReadRegStr("HostUrl");
    s_cfg.enrollment_state = "not_enrolled";

    std::ifstream f(ConfigPath(data_dir));
    if (f.is_open()) {
        try {
            json j = json::parse(f, nullptr, false);
            if (j.is_object()) {
                s_cfg.sensor_id = j.value("sensor_id", "");
                if (s_cfg.host_url.empty()) s_cfg.host_url = j.value("host_url", "");
                s_cfg.enrollment_state = j.value("enrollment_state", s_cfg.enrollment_state);
                s_cfg.enrolled_host_name = j.value("enrolled_host_name", "");
                s_cfg.enrolled_host_instance = j.value("enrolled_host_instance", "");
                s_cfg.enrolled_host_address = j.value("enrolled_host_address", "");
                s_cfg.generated_at_ms = j.value("generated_at_ms", (int64_t)0);
                s_cfg.enrolled_at_ms = j.value("enrolled_at_ms", (int64_t)0);
            }
        } catch (...) {
            DiagnosticLogWarn("Enterprise enrollment config parse failed; regenerating safe defaults.");
        }
    }

    if (s_cfg.sensor_id.empty()) s_cfg.sensor_id = NewGuidString();
    if (s_cfg.generated_at_ms <= 0) s_cfg.generated_at_ms = NowMs();
    SaveConfig();
    DiagnosticLogInfo("Enterprise config initialized: mode=" + s_cfg.install_mode +
                      ", sensor_id_hash=" + ShortPublicHash(s_cfg.sensor_id));
    return true;
}

const EnterpriseConfig& GetEnterpriseConfig() {
    return s_cfg;
}

json EnterpriseConfigJson(bool public_view) {
    json j;
    j["product"] = "TelemetryApp";
    j["hostname"] = LocalHostname();
    j["install_mode"] = s_cfg.install_mode;
    j["enrollment_state"] = s_cfg.enrollment_state;
    j["sensor_id_hash"] = ShortPublicHash(s_cfg.sensor_id);
    j["device_id"] = ShortPublicHash(s_cfg.sensor_id);
    j["mac_hash"] = PrimaryMacHash();
    j["host_url_configured"] = !s_cfg.host_url.empty();
    j["remote_tls_required"] = true;
    j["current_transport_secure"] = false;
    j["lab_enrollment_supported"] = true;
    j["supported_protocols"] = {"https-mtls-planned", "manual-add", "inventory-import", "mdns-candidate-planned"};
    j["generated_at_ms"] = s_cfg.generated_at_ms;
    if (s_cfg.enrolled_at_ms > 0) {
        j["enrolled_at_ms"] = s_cfg.enrolled_at_ms;
        j["enrolled_host_name"] = s_cfg.enrolled_host_name;
    }
    if (!public_view) {
        j["sensor_id"] = s_cfg.sensor_id;
        j["host_url"] = s_cfg.host_url;
        j["data_dir"] = s_cfg.data_dir;
        j["enrolled_host_instance"] = s_cfg.enrolled_host_instance;
        j["enrolled_host_address"] = s_cfg.enrolled_host_address;
    }
    return j;
}

json InstallAuditJson() {
    json j = EnterpriseConfigJson(false);
    j["registry_key"] = "HKLM\\SOFTWARE\\TelemetryApp";
    j["sensor_config_path"] = ConfigPath(s_cfg.data_dir);
    j["secrets_policy"] = "Enrollment tokens and API secrets must not be persisted in registry, environment variables, or diagnostic logs.";
    return j;
}

bool RecordLabEnrollment(const std::string& host_name,
                         const std::string& host_instance,
                         const std::string& host_address) {
    s_cfg.enrollment_state = "enrolled_lab";
    s_cfg.enrolled_host_name = host_name;
    s_cfg.enrolled_host_instance = host_instance;
    s_cfg.enrolled_host_address = host_address;
    if (host_address.rfind("http://", 0) == 0 || host_address.rfind("https://", 0) == 0) {
        s_cfg.host_url = host_address;
    }
    s_cfg.enrolled_at_ms = NowMs();
    SaveConfig();
    DiagnosticLogInfo("Lab enrollment accepted for host=" + host_name +
                      ", host_instance=" + host_instance +
                      ", host_address=" + host_address);
    return true;
}

} // namespace Service
