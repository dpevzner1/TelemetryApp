#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <mutex>
#include <functional>

namespace Service {

enum class ExpiryType : int {
    Permanent = 0,
    Session   = 1,   // expires when service process exits
    Week      = 2,   // 7 days from creation
    Month     = 3,   // 30 days from creation
    Custom    = 4    // user-supplied absolute UTC ms
};

struct ApiKey {
    std::string  id;           // UUID v4 identifier
    std::string  name;         // human label
    std::string  key_value;    // the bearer token (shown once, then hashed in storage)
    std::string  key_hash;     // SHA-256 hex of key_value (stored)
    std::string  key_prefix;   // first 8 chars — safe to display
    int64_t      created_at;   // Unix milliseconds UTC
    ExpiryType   expiry_type;
    int64_t      expires_at;   // Unix ms, 0 = permanent / session
    bool         active;
};

// On-change callback: regenerates API.md in the install directory
using OnChangeCb = std::function<void()>;

class ApiKeyStore {
public:
    explicit ApiKeyStore(const std::string& store_path,
                         const std::string& api_md_path);

    bool Load();
    bool Save();

    // Create a new key. Returns the full plaintext key (only time it is returned).
    std::string Create(const std::string& name, ExpiryType expiry, int64_t custom_expiry_ms = 0);

    // Validate an incoming bearer token. Returns true if valid and not expired.
    bool Validate(const std::string& bearer_token);

    // Delete by ID. Returns false if not found.
    bool Delete(const std::string& key_id);

    // Rotate: delete existing, create replacement with same name/expiry.
    // Returns the new plaintext key.
    std::string Rotate(const std::string& key_id);

    const std::vector<ApiKey>& Keys() const;

    void SetOnChange(OnChangeCb cb) { m_on_change = std::move(cb); }

    // Generate API.md documenting current keys and all endpoints.
    bool GenerateApiMd(const std::string& service_url) const;

    // Purge expired session keys (call on service startup)
    void PurgeSessionKeys();

private:
    std::string             m_store_path;
    std::string             m_api_md_path;
    std::vector<ApiKey>     m_keys;
    mutable std::mutex      m_mu;
    OnChangeCb              m_on_change;

    static std::string GenerateRawKey();
    static std::string Hash(const std::string& raw);
    static std::string NewUuid();
    static int64_t     NowMs();
    static int64_t     ExpiryMs(ExpiryType t, int64_t custom_ms);
    static bool        IsExpired(const ApiKey& k);
};

// Singleton accessor (call after Init)
ApiKeyStore& GetKeyStore();
bool KeyStoreInit(const std::string& store_path, const std::string& api_md_path);

} // namespace Service
