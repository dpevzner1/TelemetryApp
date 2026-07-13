#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <random>
#include <chrono>
#include <algorithm>
#include "api_key_store.h"

#pragma comment(lib, "bcrypt.lib")

using json = nlohmann::json;
using namespace std::chrono;

namespace Service {

static ApiKeyStore* s_store = nullptr;

// ── Utilities ─────────────────────────────────────────────────────────────────

int64_t ApiKeyStore::NowMs() {
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string ApiKeyStore::NewUuid() {
    // Use BCryptGenRandom for cryptographic quality
    uint8_t buf[16]{};
    BCryptGenRandom(nullptr, buf, 16, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    buf[6] = (buf[6] & 0x0F) | 0x40; // version 4
    buf[8] = (buf[8] & 0x3F) | 0x80; // variant
    char uuid[37];
    snprintf(uuid, sizeof(uuid),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        buf[0],buf[1],buf[2],buf[3],buf[4],buf[5],buf[6],buf[7],
        buf[8],buf[9],buf[10],buf[11],buf[12],buf[13],buf[14],buf[15]);
    return uuid;
}

std::string ApiKeyStore::GenerateRawKey() {
    // 32 random bytes → hex string = 64 chars, prefix with "tlm-"
    uint8_t buf[32]{};
    BCryptGenRandom(nullptr, buf, 32, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    std::ostringstream ss;
    ss << "tlm-";
    for (auto b : buf) ss << std::hex << std::setw(2) << std::setfill('0') << (int)b;
    return ss.str();
}

std::string ApiKeyStore::Hash(const std::string& raw) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    DWORD hashLen = 0, cbData = 0;
    BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PUCHAR)&hashLen, sizeof(hashLen), &cbData, 0);
    BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0);
    BCryptHashData(hHash, (PUCHAR)raw.data(), (ULONG)raw.size(), 0);
    std::vector<uint8_t> digest(hashLen);
    BCryptFinishHash(hHash, digest.data(), hashLen, 0);
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    std::ostringstream ss;
    for (auto b : digest) ss << std::hex << std::setw(2) << std::setfill('0') << (int)b;
    return ss.str();
}

int64_t ApiKeyStore::ExpiryMs(ExpiryType t, int64_t custom_ms) {
    switch (t) {
    case ExpiryType::Permanent: return 0;
    case ExpiryType::Session:   return 0;   // checked by PurgeSessionKeys on restart
    case ExpiryType::Week:      return NowMs() + (int64_t)7 * 24 * 3600 * 1000;
    case ExpiryType::Month:     return NowMs() + (int64_t)30 * 24 * 3600 * 1000;
    case ExpiryType::Custom:    return custom_ms;
    default:                    return 0;
    }
}

bool ApiKeyStore::IsExpired(const ApiKey& k) {
    if (!k.active) return true;
    if (k.expiry_type == ExpiryType::Permanent) return false;
    if (k.expiry_type == ExpiryType::Session)   return false; // PurgeSessionKeys handles this
    if (k.expires_at == 0) return false;
    return NowMs() > k.expires_at;
}

// ── Constructor ───────────────────────────────────────────────────────────────

ApiKeyStore::ApiKeyStore(const std::string& store_path, const std::string& api_md_path)
    : m_store_path(store_path), m_api_md_path(api_md_path) {}

// ── Load / Save ───────────────────────────────────────────────────────────────

bool ApiKeyStore::Load() {
    std::lock_guard<std::mutex> lk(m_mu);
    m_keys.clear();
    std::ifstream f(m_store_path);
    if (!f.is_open()) return true; // first run: no file = empty store
    try {
        json j = json::parse(f);
        for (auto& item : j["keys"]) {
            ApiKey k;
            k.id          = item["id"].get<std::string>();
            k.name        = item["name"].get<std::string>();
            k.key_hash    = item["key_hash"].get<std::string>();
            k.key_prefix  = item["key_prefix"].get<std::string>();
            k.key_value   = ""; // never stored plaintext
            k.created_at  = item["created_at"].get<int64_t>();
            k.expiry_type = static_cast<ExpiryType>(item["expiry_type"].get<int>());
            k.expires_at  = item["expires_at"].get<int64_t>();
            k.active      = item["active"].get<bool>();
            m_keys.push_back(k);
        }
    } catch (...) {
        spdlog::warn("api_key_store: parse error in {}", m_store_path);
    }
    return true;
}

bool ApiKeyStore::Save() {
    json j;
    j["schema_version"] = 1;
    j["keys"] = json::array();
    for (const auto& k : m_keys) {
        j["keys"].push_back({
            {"id",          k.id},
            {"name",        k.name},
            {"key_hash",    k.key_hash},
            {"key_prefix",  k.key_prefix},
            {"created_at",  k.created_at},
            {"expiry_type", static_cast<int>(k.expiry_type)},
            {"expires_at",  k.expires_at},
            {"active",      k.active}
        });
    }
    std::ofstream f(m_store_path);
    if (!f.is_open()) return false;
    f << j.dump(2);
    if (m_on_change) m_on_change();
    return true;
}

// ── CRUD ──────────────────────────────────────────────────────────────────────

std::string ApiKeyStore::Create(const std::string& name, ExpiryType expiry, int64_t custom_ms) {
    std::lock_guard<std::mutex> lk(m_mu);
    std::string raw = GenerateRawKey();
    ApiKey k;
    k.id          = NewUuid();
    k.name        = name;
    k.key_value   = raw;
    k.key_hash    = Hash(raw);
    k.key_prefix  = raw.substr(0, 12); // "tlm-XXXXXXXX"
    k.created_at  = NowMs();
    k.expiry_type = expiry;
    k.expires_at  = ExpiryMs(expiry, custom_ms);
    k.active      = true;
    m_keys.push_back(k);
    Save();
    return raw; // plaintext returned only here
}

bool ApiKeyStore::Validate(const std::string& bearer) {
    std::lock_guard<std::mutex> lk(m_mu);
    // Constant-time comparison via hashing both sides
    std::string h = Hash(bearer);
    for (const auto& k : m_keys) {
        if (!k.active) continue;
        if (IsExpired(k)) continue;
        if (k.key_hash == h) return true;
    }
    return false;
}

bool ApiKeyStore::Delete(const std::string& key_id) {
    std::lock_guard<std::mutex> lk(m_mu);
    auto it = std::find_if(m_keys.begin(), m_keys.end(),
        [&](const ApiKey& k){ return k.id == key_id; });
    if (it == m_keys.end()) return false;
    m_keys.erase(it);
    Save();
    return true;
}

std::string ApiKeyStore::Rotate(const std::string& key_id) {
    std::lock_guard<std::mutex> lk(m_mu);
    auto it = std::find_if(m_keys.begin(), m_keys.end(),
        [&](const ApiKey& k){ return k.id == key_id; });
    if (it == m_keys.end()) return "";

    std::string name        = it->name;
    ExpiryType  expiry      = it->expiry_type;
    int64_t     expires_at  = it->expires_at;
    m_keys.erase(it);

    std::string raw = GenerateRawKey();
    ApiKey k;
    k.id          = NewUuid();
    k.name        = name;
    k.key_value   = raw;
    k.key_hash    = Hash(raw);
    k.key_prefix  = raw.substr(0, 12);
    k.created_at  = NowMs();
    k.expiry_type = expiry;
    k.expires_at  = (expiry == ExpiryType::Week || expiry == ExpiryType::Month)
                    ? ExpiryMs(expiry, 0)   // reset timer from now
                    : expires_at;
    k.active      = true;
    m_keys.push_back(k);
    Save();
    return raw;
}

const std::vector<ApiKey>& ApiKeyStore::Keys() const { return m_keys; }

void ApiKeyStore::PurgeSessionKeys() {
    std::lock_guard<std::mutex> lk(m_mu);
    bool changed = false;
    for (auto& k : m_keys) {
        if (k.expiry_type == ExpiryType::Session && k.active) {
            k.active = false;
            changed = true;
        }
    }
    if (changed) Save();
}

// ── API.md generation ─────────────────────────────────────────────────────────

static std::string FmtMs(int64_t ms) {
    if (ms == 0) return "N/A";
    time_t t = ms / 1000;
    char buf[32]{};
    struct tm tm_val{};
    gmtime_s(&tm_val, &t);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M UTC", &tm_val);
    return buf;
}

static const char* ExpiryLabel(ExpiryType t) {
    switch (t) {
    case ExpiryType::Permanent: return "Permanent";
    case ExpiryType::Session:   return "Session";
    case ExpiryType::Week:      return "7 Days";
    case ExpiryType::Month:     return "30 Days";
    case ExpiryType::Custom:    return "Custom";
    default:                    return "Unknown";
    }
}

bool ApiKeyStore::GenerateApiMd(const std::string& service_url) const {
    std::ofstream f(m_api_md_path);
    if (!f.is_open()) return false;

    auto now = FmtMs(NowMs());
    f << "# TelemetryApp — API Reference\n\n";
    f << "> Auto-generated by TelemetryApp v1.0.0 on " << now << "  \n";
    f << "> This file is regenerated whenever API keys change. Do not edit manually.\n\n";
    f << "---\n\n";

    f << "## Base URL\n\n";
    f << "```\n" << service_url << "\n```\n\n";

    f << "## Authentication\n\n";
    f << "All endpoints except `/metrics`, `/api/v1/health`, `/api/v1/enrollment/readiness`, `/api/v1/enrollment/request`, and Fleet Manager heartbeat intake `/api/v1/fleet/heartbeat` require the following header:\n\n";
    f << "```\nX-API-Key: <your-api-key>\n```\n\n";
    f << "API keys are managed via the **TelemetryApp GUI → API** panel,  \n";
    f << "or via `POST /api/v1/keys` with an existing admin key.\n\n";
    f << "Authentication accepts any of these forms. Prefer headers for automation:\n\n";
    f << "- `X-API-Key: <your-api-key>`\n";
    f << "- `Authorization: Bearer <your-api-key>`\n";
    f << "- `?api_key=<your-api-key>` query parameter for constrained tools only\n\n";

    f << "---\n\n";
    f << "## AI / Script Immediate Use Contract\n\n";
    f << "A script or another AI tool can use TelemetryApp immediately with this sequence:\n\n";
    f << "1. Call `GET /api/v1/health` to confirm the service is reachable.\n";
    f << "2. Call `GET /api/v1/hardware` to identify CPU/GPU make, model, topology, and supported capability quality.\n";
    f << "3. Call `GET /api/v1/metrics/catalog` to map stable metric IDs.\n";
    f << "4. Optionally call `POST /api/v1/watch` to add a process by `pid` or `exe_name`.\n";
    f << "5. Call `POST /api/v1/sessions` with `label`, optional absolute `log_dir`, and optional `metric_ids`.\n";
    f << "6. Run the workload.\n";
    f << "6. Call `POST /api/v1/sessions/<id>/stop` and read the returned `log_path`.\n\n";
    f << "Session logs are JSONL files named `sess-<id>.jsonl`; metadata is stored as `sess-<id>.meta.json`. If `log_dir` is omitted, files go to `%TELEMETRY_DATA_DIR%\\logs\\sessions\\`. If `log_dir` is supplied, it must be an absolute Windows path and the service account must have write access.\n\n";
    f << "Current remote fleet truth: local sessions are live; discovered sensors can be explicitly enrolled for lab snapshot/logging workflows; heartbeat/call-home updates changed sensor IP addresses by stable identity. Sensor-client installs cannot discover, poll, or manage other devices.\n\n";

    f << "---\n\n";
    f << "## Endpoints\n\n";
    f << "| Method | Path | Auth | Description |\n";
    f << "|--------|------|------|-------------|\n";
    f << "| GET | `/api/v1/snapshot` | Required | Full current telemetry snapshot (JSON) |\n";
    f << "| GET | `/api/v1/history/<id>?n=300` | Required | Last N samples for metric ID |\n";
    f << "| GET | `/api/v1/stream` | Required | Server-Sent Events real-time push (~1Hz) |\n";
    f << "| GET | `/api/v1/health` | None | Service liveness + poll duration |\n";
    f << "| GET | `/metrics` | None | Prometheus text exposition format |\n";
    f << "| GET | `/api/v1/capabilities` | Required | Service, device, and accelerator capabilities |\n";
    f << "| GET | `/api/v1/hardware` | Required | Hardware identity registry: CPU/GPU make, model, topology, provider preference, and per-device capability status |\n";
    f << "| GET | `/api/v1/metrics/catalog` | Required | Stable metric IDs and common metric names |\n";
    f << "| GET | `/api/v1/diagnostics` | Required | Service diagnostics and runtime counters |\n";
    f << "| GET | `/api/v1/enrollment/readiness` | None | Non-secret sensor readiness metadata for manual add/candidate discovery |\n";
    f << "| POST | `/api/v1/enrollment/request` | None | Explicit lab enrollment request; validates public sensor fingerprint and records sensor acknowledgement |\n";
    f << "| POST | `/api/v1/fleet/heartbeat` | None, Fleet Manager only | Sensor call-home inventory update; reconciles IP changes by device/sensor/MAC hash and never auto-trusts |\n";
    f << "| GET | `/api/v1/lab/snapshot` | None after enrollment | Lab-enrolled remote snapshot for Fleet View |\n";
    f << "| POST | `/api/v1/lab/sessions` | None after enrollment | Lab-enrolled remote logging session start |\n";
    f << "| POST | `/api/v1/lab/sessions/<id>/stop` | None after enrollment | Lab-enrolled remote logging session stop |\n";
    f << "| GET | `/api/v1/install/audit` | Required | Local install mode, sensor identity, registry/config audit metadata |\n";
    f << "| GET | `/api/v1/keys` | Required | List all API keys (prefixes only) |\n";
    f << "| POST | `/api/v1/keys` | Required | Create a new API key |\n";
    f << "| DELETE | `/api/v1/keys/<id>` | Required | Delete an API key |\n";
    f << "| POST | `/api/v1/keys/<id>/rotate` | Required | Rotate (replace) an API key |\n";
    f << "| GET | `/api/v1/watch` | Required | List watched processes |\n";
    f << "| POST | `/api/v1/watch` | Required | Add a process to the watch list |\n";
    f << "| DELETE | `/api/v1/watch/<pid>` | Required | Remove a process from the watch list |\n";
    f << "| GET | `/api/v1/sessions` | Required | List logging sessions |\n";
    f << "| POST | `/api/v1/sessions` | Required | Start a logging session with optional custom folder/process watch |\n";
    f << "| GET | `/api/v1/sessions/<id>` | Required | Read one logging session |\n";
    f << "| POST | `/api/v1/sessions/<id>/stop` | Required | Stop a logging session |\n";
    f << "| DELETE | `/api/v1/sessions/<id>` | Required | Delete a logging session record |\n";

    f << "\n## Snapshot GPU / Accelerator Fields\n\n";
    f << "`GET /api/v1/snapshot` returns `gpus[]` entries with these accelerator fields when available:\n\n";
    f << "| Field | Meaning |\n";
    f << "|-------|---------|\n";
    f << "| `sm_util_pct` | NVIDIA SM/compute utilization from NVML where available |\n";
    f << "| `mem_bw_util_pct` | GPU memory bandwidth utilization from NVML where available |\n";
    f << "| `mem_clk_transitions` | Memory clock transition count observed by the service |\n";
    f << "| `has_tensor_cores` | Boolean tensor-core presence inference; currently NVIDIA/NVML based |\n";
    f << "| `cuda_cc_major` / `cuda_cc_minor` | CUDA compute capability reported by NVML |\n";
    f << "| `tensor_core_gen` | Inferred tensor-core generation from CUDA compute capability |\n\n";

    f << "`GET /api/v1/capabilities` returns an `accelerators[]` array containing adapter name, tensor-core inference, CUDA compute capability, tensor-core generation, and the metric IDs for SM and memory-bandwidth utilization.\n\n";
    f << "`GET /api/v1/hardware` is the richer hardware identity registry. It reports CPU vendor/model/family/stepping, logical and physical topology where Windows exposes it, cache topology, CPU instruction sets, DXGI GPU vendor/device IDs, adapter memory, current provider preference, and per-device capability objects. Capability objects distinguish `measured`, `derived`, `inferred`, `unavailable`, `unsupported`, and `not_implemented`; unsupported values must not be interpreted as zero.\n\n";

    f << "## Power / Electrical Telemetry\n\n";
    f << "`GET /api/v1/snapshot` includes a root `power` object. Electrical values are source-qualified and must be interpreted by `quality`:\n\n";
    f << "| Quality | Meaning |\n";
    f << "|---------|---------|\n";
    f << "| `measured` | Direct vendor/platform reading, such as NVML GPU watts. |\n";
    f << "| `derived` | Computed from measured samples, such as future session energy. |\n";
    f << "| `estimated` | Model-based approximation; not direct electrical measurement. |\n";
    f << "| `unavailable` | Provider not present, unsupported, or not implemented. |\n\n";
    f << "Current implementation exposes source-qualified GPU power where available, CPU package power as unavailable until a CPU provider is added, platform power as unavailable unless a battery/external-meter provider is added, and per-process power as a future estimated attribution path.\n\n";
    f << "`GET /metrics` includes `telemetry_cpu_package_power_watts` and `telemetry_platform_power_watts` with `source` and `quality` labels. GPU power metrics include `source` and `quality` labels when active GPU adapters are reported.\n\n";

    f << "## Enterprise Sensor Enrollment Contract\n\n";
    f << "`GET /api/v1/enrollment/readiness` is intentionally public and returns only non-secret candidate metadata: product, hostname, install mode, sensor ID hash, MAC hash, host URL configured state, and remote TLS requirement. Discovery is not trust. Raw MAC addresses are reserved for trusted/enrolled inventory APIs because they are stable hardware identifiers.\n\n";
    f << "`POST /api/v1/enrollment/request` accepts explicit lab enrollment when `accept_lab_enrollment:true` and the submitted `sensor_id_hash` matches the readiness fingerprint. If a `mac_hash` is supplied, it must also match. Accepted enrollment records `enrolled_lab` and stores the Fleet Host URL for later sensor call-home.\n\n";
    f << "`POST /api/v1/fleet/heartbeat` is accepted only by a Fleet Manager install. A sensor with configured `host_url` posts public identity metadata (`device_id`, `sensor_id_hash`, `mac_hash`, `hostname`, `install_mode`, `enrollment_state`, `api_port`). The Fleet Manager derives the current `ip:port` from the TCP peer address plus `api_port`, merges by stable identity, updates `last_seen_address`, `last_seen_at_ms`, and `address_history`, and never grants trust by heartbeat alone.\n\n";
    f << "Fleet heartbeat is a lab inventory continuity feature, not enterprise cryptographic trust. A future release must add one-time token validation, TLS/mTLS, certificate/thumbprint pinning, and token invalidation before accepting remote telemetry as enterprise-grade.\n\n";
    f << "`GET /api/v1/install/audit` requires API auth and returns local install mode, full sensor ID, data path, and the secrets policy. Enrollment tokens and API secrets must not be persisted in registry, environment variables, or diagnostic logs.\n\n";

    f << "Example session start with a custom log folder:\n\n";
    f << "```json\n";
    f << "{\n";
    f << "  \"label\": \"training-run\",\n";
    f << "  \"log_dir\": \"D:\\\\TelemetryRuns\\\\training-run\",\n";
    f << "  \"metric_ids\": [],\n";
    f << "  \"watch\": { \"pids\": [12345], \"exe_names\": [] },\n";
    f << "  \"stop_policy\": { \"mode\": \"process_exit_or_duration\", \"max_duration_seconds\": 7200 }\n";
    f << "}\n";
    f << "```\n";

    f << "\n---\n\n";
    f << "## Active API Keys\n\n";
    f << "| Name | Prefix | Created | Expiry Type | Expires At | Status |\n";
    f << "|------|--------|---------|-------------|------------|--------|\n";

    for (const auto& k : m_keys) {
        std::string status = k.active ? (IsExpired(k) ? "Expired" : "Active") : "Revoked";
        f << "| " << k.name
          << " | `" << k.key_prefix << "...`"
          << " | " << FmtMs(k.created_at)
          << " | " << ExpiryLabel(k.expiry_type)
          << " | " << (k.expires_at ? FmtMs(k.expires_at) : "—")
          << " | " << status << " |\n";
    }
    if (m_keys.empty()) f << "| *(no keys configured)* | — | — | — | — | — |\n";

    f << "\n---\n\n";
    f << "## Quick Start\n\n";
    f << "```bash\n";
    f << "# Health check (no auth)\n";
    f << "curl " << service_url << "/api/v1/health\n\n";
    f << "# Current snapshot\n";
    f << "curl -H \"X-API-Key: <key>\" " << service_url << "/api/v1/snapshot\n\n";
    f << "# Device and accelerator capabilities\n";
    f << "curl -H \"X-API-Key: <key>\" " << service_url << "/api/v1/capabilities\n\n";
    f << "# Hardware make/model/topology/capability registry\n";
    f << "curl -H \"X-API-Key: <key>\" " << service_url << "/api/v1/hardware\n\n";
    f << "# Stable metric catalog\n";
    f << "curl -H \"X-API-Key: <key>\" " << service_url << "/api/v1/metrics/catalog\n\n";
    f << "# Start a logging session\n";
    f << "curl -X POST -H \"X-API-Key: <key>\" -H \"Content-Type: application/json\" -d @session.json " << service_url << "/api/v1/sessions\n\n";
    f << "# CPU utilization history (last 60 samples)\n";
    f << "curl -H \"X-API-Key: <key>\" " << service_url << "/api/v1/history/0?n=60\n\n";
    f << "# Prometheus scrape\n";
    f << "curl " << service_url << "/metrics\n";
    f << "```\n\n";

    f << "## Environment Variables (set by installer)\n\n";
    f << "| Variable | Value | Description |\n";
    f << "|----------|-------|-------------|\n";
    f << "| `TELEMETRY_API_URL` | `" << service_url << "` | Base URL for all API calls |\n";
    f << "| `TELEMETRY_APP_DIR` | *(install dir)* | Directory containing the executables |\n";
    f << "| `TELEMETRY_DATA_DIR` | *(data dir)* | Logs, configs, and API key store |\n";
    f << "| `TELEMETRY_REMOTE_API` | unset/false | When enabled, binds HTTP to 0.0.0.0; otherwise localhost only |\n";
    f << "| `TELEMETRY_FIREWALL_RULES_ENABLED` | installer value | When enabled, setup creates grouped `TelemetryApp` Windows Defender Firewall rules. The inbound service API rule is Private/Domain only and scoped to `LocalSubnet` by default. |\n";
    f << "\n";
    f << "Firewall note: Remote readiness/manual-add testing requires the sensor service to bind beyond loopback and Windows Defender Firewall or enterprise policy to allow inbound TCP 8765 to `telemetry_service.exe`. Installer-created inbound access is intentionally restricted to Private/Domain profiles and `LocalSubnet`; routed VLAN/subnet access should be provided through administrator-managed policy with an explicit remote address range.\n";
    f << "\n";
    f << "> This file is located at: `<install_dir>\\API.md`  \n";
    f << "> Regenerated automatically. Source of truth: `%TELEMETRY_DATA_DIR%\\api_keys\\store.json`\n";

    return true;
}

// ── Singleton ─────────────────────────────────────────────────────────────────

ApiKeyStore& GetKeyStore() { return *s_store; }

bool KeyStoreInit(const std::string& store_path, const std::string& api_md_path) {
    static ApiKeyStore inst(store_path, api_md_path);
    s_store = &inst;
    if (!inst.Load()) return false;
    inst.PurgeSessionKeys();
    return true;
}

} // namespace Service
