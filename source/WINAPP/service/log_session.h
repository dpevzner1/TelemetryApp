#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <mutex>
#include <memory>
#include <cstdint>

// Forward-declare ShmBlock to avoid pulling in the full SHM layout header
// in every translation unit that includes this header.
struct ShmBlock;

namespace Service {

enum class SessionStatus : int { Active = 0, Stopped = 1, Failed = 2 };

struct LogSession {
    std::string     id;            // "sess-{8 hex chars}"
    std::string     label;         // caller-provided label, e.g. "training-run-001"
    int64_t         started_at_ms  = 0;
    int64_t         ended_at_ms    = 0;
    SessionStatus   status         = SessionStatus::Active;
    std::vector<uint32_t> metric_ids; // empty = log all standard metrics
    std::string     log_dir;       // directory where log file resides (absolute)
    std::string     log_path;      // absolute path to .jsonl file
    std::string     meta_path;     // absolute path to .meta.json file
    int64_t         duration_ms    = 0; // 0 = explicit stop
    bool            stop_when_watch_empty = false;
    bool            watch_seen     = false;
    uint64_t        rows_written   = 0;
    uint64_t        seq            = 0; // monotonic per-row counter

    std::ofstream   file;          // kept open for duration of active session
};

// ── LogSessionStore ──────────────────────────────────────────────────────────
//
// Singleton that manages named logging sessions.
// Designed to be called from two threads:
//   • Poll thread  → OnPollTick()
//   • HTTP threads → Start / Stop / Delete / List / Find
//
// A 4-byte random session ID is generated per session (8 hex chars, "sess-xxxx").
// Log files live in: {data_dir}\logs\sessions\{id}.jsonl
// Meta files live in: {data_dir}\logs\sessions\{id}.meta.json
// An append-only session index lives at: {data_dir}\logs\sessions\index.jsonl
//
class LogSessionStore {
public:
    // Must be called once at service start before any other method.
    void Init(const std::string& data_dir);

    // Start a new logging session.
    // label      : human-readable name for this session (stored in each row)
    // metric_ids : empty = log all standard metrics; non-empty = log only these IDs
    // log_dir    : absolute path to write log files; empty = use default data_dir\logs\sessions\
    // Returns session_id on success, empty string on failure.
    std::string Start(const std::string& label,
                      const std::vector<uint32_t>& metric_ids,
                      const std::string& log_dir = "",
                      int64_t duration_ms = 0,
                      bool stop_when_watch_empty = false);

    // Stop an active session: flushes file, closes it, stamps ended_at.
    // Returns the absolute log file path on success, empty string if not found.
    std::string Stop(const std::string& session_id);

    // Delete a session entry and its log + meta files.
    // Returns false if session_id not found.
    bool Delete(const std::string& session_id);

    // Snapshot of all sessions (copies to avoid holding lock during serialisation).
    struct SessionSummary {
        std::string   id, label, log_path, log_dir;
        SessionStatus status;
        int64_t       started_at_ms, ended_at_ms;
        int64_t       duration_ms;
        bool          stop_when_watch_empty;
        uint64_t      rows_written;
        std::vector<uint32_t> metric_ids;
    };
    std::vector<SessionSummary> List() const;
    bool FindSummary(const std::string& id, SessionSummary& out) const;

    // Called from the poll loop once per tick (after ShmEndWrite).
    // Writes one JSONL row per active session.
    void OnPollTick(ShmBlock* shm);

    static LogSessionStore& Instance();

private:
    std::string  m_sessions_dir;
    bool         m_initialised = false;

    mutable std::mutex                            m_mu;
    std::vector<std::unique_ptr<LogSession>>      m_sessions;

    // Internal helpers — caller must hold m_mu
    LogSession* FindLocked(const std::string& id);
    void        StopLocked(LogSession& s, int64_t ended_at_ms);
    void        WriteMeta(LogSession& s);
    void        AppendIndex(const LogSession& s);
    void        WriteRow(LogSession& s, ShmBlock* shm);

    static std::string GenerateId();  // returns "sess-{8 hex}"
    static int64_t     NowMs();
    static void        EnsureDir(const std::string& path);
};

} // namespace Service
