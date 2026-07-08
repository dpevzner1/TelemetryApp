#pragma once
#include "../renderer/d2d_context.h"
#include <functional>
#include <string>
#include <vector>

namespace Client {

struct FleetDeviceRow {
    std::string name;
    std::string role;
    std::string address;
    std::string os;
    std::string state;
    std::string sensor_hash;
    std::string mac_hash;
    std::string last_error;
    bool trusted = false;
    bool local = false;
};

struct FleetLoggingJob {
    std::string id;
    std::string label;
    std::string target_mode = "Local trusted device";
    std::string schedule = "Manual start";
    std::string storage_dir;
    std::string partition = "Daily per-device logs + fleet index";
    std::string format = "jsonl";
    std::string verbosity = "Balanced";
    std::string status = "Draft";
    std::string last_error;
    std::string schedule_kind = "Manual";
    std::string recurrence = "Weekly";
    std::string one_time_date;
    int days_mask = 62; // Mon-Fri
    int start_hour = 9;
    int end_hour = 17;
    int target_count = 1;
    int rows_written = 0;
    std::vector<std::string> remote_session_refs;
    bool local_only = true;
    bool all_day = false;
};

class FleetPage {
public:
    explicit FleetPage(D2DContext& ctx);

    using SimpleCb = std::function<void()>;
    using DeviceCb = std::function<void(const FleetDeviceRow&)>;
    using ChooseFolderCb = std::function<bool(std::string&)>;
    using JobCb = std::function<bool(const FleetLoggingJob&, std::string&)>;

    void SetOnViewLocal(SimpleCb cb) { m_on_view_local = std::move(cb); }
    void SetOnViewRemote(DeviceCb cb) { m_on_view_remote = std::move(cb); }
    void SetOnChooseLogFolder(ChooseFolderCb cb) { m_on_choose_log_folder = std::move(cb); }
    void SetOnStartLocalJob(JobCb cb) { m_on_start_local_job = std::move(cb); }
    void SetOnStopLocalJob(SimpleCb cb) { m_on_stop_local_job = std::move(cb); }
    void SetOnDevicesChanged(SimpleCb cb) { m_on_devices_changed = std::move(cb); }

    void SetServiceConnected(bool connected) { m_service_connected = connected; }
    void SetLocalHost(const std::string& hostname);
    void SetStoragePath(const std::string& path);
    const std::vector<FleetDeviceRow>& Devices() const { return m_devices; }

    void Draw(float x, float y, float w, float h, float dpi_scale);
    void OnClick(float x, float y);
    void OnScroll(float delta);
    void OnMouseMove(float x, float y);
    void OnMouseUp();
    bool IsScrollbarDragging() const { return m_scroll_dragging; }

private:
    struct ButtonRect { float x0, y0, x1, y1; int id; };

    D2DContext& m_ctx;
    std::vector<FleetDeviceRow> m_devices;
    std::vector<FleetLoggingJob> m_jobs;
    std::vector<ButtonRect> m_buttons;
    SimpleCb m_on_view_local;
    DeviceCb m_on_view_remote;
    ChooseFolderCb m_on_choose_log_folder;
    JobCb m_on_start_local_job;
    SimpleCb m_on_stop_local_job;
    SimpleCb m_on_devices_changed;

    bool m_service_connected = false;
    bool m_discovery_attempted = false;
    bool m_discovery_running = false;
    std::string m_discovery_status;
    bool m_wizard_open = false;
    int m_wizard_step = 0;
    FleetLoggingJob m_draft;
    std::string m_jobs_path;
    std::string m_devices_path;
    std::string m_running_job_id;

    float m_scroll_y = 0.0f;
    float m_content_h = 0.0f;
    float m_view_h = 0.0f;
    bool  m_scroll_dragging = false;
    float m_scroll_drag_offset = 0.0f;
    float m_scroll_rail_x0 = 0.0f;
    float m_scroll_rail_y0 = 0.0f;
    float m_scroll_rail_x1 = 0.0f;
    float m_scroll_rail_y1 = 0.0f;
    float m_scroll_thumb_y0 = 0.0f;
    float m_scroll_thumb_y1 = 0.0f;

    static constexpr int BTN_SEARCH = 1;
    static constexpr int BTN_VIEW_LOCAL = 2;
    static constexpr int BTN_ADD_MANUAL = 3;
    static constexpr int BTN_NEW_JOB = 4;
    static constexpr int BTN_DEVICE_VIEW_BASE = 400;
    static constexpr int BTN_DEVICE_REFRESH_BASE = 600;
    static constexpr int BTN_DEVICE_DELETE_BASE = 800;
    static constexpr int BTN_DEVICE_ENROLL_BASE = 1600;
    static constexpr int BTN_JOB_START_BASE = 1000;
    static constexpr int BTN_JOB_STOP_BASE = 1200;
    static constexpr int BTN_JOB_DELETE_BASE = 1400;
    static constexpr int BTN_WIZ_NEXT = 2000;
    static constexpr int BTN_WIZ_BACK = 2001;
    static constexpr int BTN_WIZ_CANCEL = 2002;
    static constexpr int BTN_WIZ_CREATE = 2003;
    static constexpr int BTN_WIZ_PICK_FOLDER = 2004;
    static constexpr int BTN_OPT_BASE = 3000;

    void DrawButton(float x, float y, float w, float h, const wchar_t* label,
                    D2D1_COLOR_F color, int id);
    void DrawPill(float x, float y, float w, const wchar_t* label, D2D1_COLOR_F color);
    void DrawDeviceRow(float x, float& y, float w, const FleetDeviceRow& row);
    void DrawJobs(float x, float& y, float w);
    void DrawWizard(float x, float& y, float w);
    void DrawScrollbar(float x, float y, float w, float h);
    void ClampScroll();
    void ScrollToThumbY(float y);
    void BeginWizard();
    void CreateDraftJob();
    void StartJob(size_t index);
    void StopJob(size_t index);
    void DeleteJob(size_t index);
    void ViewDevice(size_t index);
    void EnrollDevice(size_t index);
    void RefreshDevice(size_t index);
    void DeleteDevice(size_t index);
    void SearchLanCandidates();
    void ManualAddDevice();
    bool AddOrUpdateRemoteCandidate(const std::string& address, std::string& error,
                                    int connect_timeout_ms = 1200,
                                    int read_timeout_ms = 2500);
    void LoadJobs();
    void SaveJobs() const;
    void LoadDevices();
    void SaveDevices() const;
    std::string ValidationText() const;
    std::string BuildScheduleLabel() const;
    std::string BuildScheduleLabel(const FleetLoggingJob& job) const;
};

} // namespace Client
