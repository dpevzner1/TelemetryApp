#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <d2d1_1helper.h>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include "fleet_page.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <ctime>
#include <set>
#include <cstddef>

using json = nlohmann::json;

namespace Client {

static constexpr D2D1_COLOR_F kBg      = {0.09f, 0.09f, 0.11f, 1.0f};
static constexpr D2D1_COLOR_F kPanel   = {0.14f, 0.14f, 0.18f, 1.0f};
static constexpr D2D1_COLOR_F kPanel2  = {0.12f, 0.12f, 0.15f, 1.0f};
static constexpr D2D1_COLOR_F kText    = {0.90f, 0.90f, 0.93f, 1.0f};
static constexpr D2D1_COLOR_F kDim     = {0.56f, 0.58f, 0.66f, 1.0f};
static constexpr D2D1_COLOR_F kBlue    = {0.25f, 0.58f, 0.95f, 1.0f};
static constexpr D2D1_COLOR_F kGreen   = {0.22f, 0.76f, 0.38f, 1.0f};
static constexpr D2D1_COLOR_F kAmber   = {0.95f, 0.64f, 0.20f, 1.0f};
static constexpr D2D1_COLOR_F kRed     = {0.90f, 0.25f, 0.28f, 1.0f};
static constexpr D2D1_COLOR_F kBorder  = {0.23f, 0.23f, 0.28f, 1.0f};

static std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (needed <= 0) return std::wstring(s.begin(), s.end());
    std::wstring out(static_cast<size_t>(needed - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), needed);
    return out;
}

static std::string Trim(std::string s) {
    auto is_ws = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!s.empty() && is_ws(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && is_ws(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

static bool ClipboardText(std::string& out) {
    out.clear();
    if (!OpenClipboard(nullptr)) return false;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (!h) {
        CloseClipboard();
        return false;
    }
    const wchar_t* w = static_cast<const wchar_t*>(GlobalLock(h));
    if (!w) {
        CloseClipboard();
        return false;
    }
    int needed = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (needed > 1) {
        std::string tmp(static_cast<size_t>(needed - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, w, -1, tmp.data(), needed, nullptr, nullptr);
        out = Trim(tmp);
    }
    GlobalUnlock(h);
    CloseClipboard();
    return !out.empty();
}

static std::string StripUrlHost(std::string input) {
    input = Trim(input);
    const std::string http = "http://";
    const std::string https = "https://";
    if (input.rfind(http, 0) == 0) input = input.substr(http.size());
    if (input.rfind(https, 0) == 0) input = input.substr(https.size());
    size_t slash = input.find('/');
    if (slash != std::string::npos) input = input.substr(0, slash);
    return Trim(input);
}

static std::string LocalComputerNameA() {
    char name[MAX_COMPUTERNAME_LENGTH + 1]{};
    DWORD len = sizeof(name);
    if (GetComputerNameA(name, &len) && name[0]) return name;
    return "TelemetryApp Fleet Host";
}

static int64_t NowMs() {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u{ ft.dwLowDateTime, ft.dwHighDateTime };
    return static_cast<int64_t>((u.QuadPart - 116444736000000000ULL) / 10000ULL);
}

static std::string PreferredFleetHostUrl() {
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return "http://localhost:8765";
    std::string out = "http://localhost:8765";
    char hostname[256]{};
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* result = nullptr;
        if (getaddrinfo(hostname, nullptr, &hints, &result) == 0) {
            for (addrinfo* it = result; it; it = it->ai_next) {
                auto* sa = reinterpret_cast<sockaddr_in*>(it->ai_addr);
                if (!sa) continue;
                unsigned char* b = reinterpret_cast<unsigned char*>(&sa->sin_addr.S_un.S_addr);
                if (b[0] == 127 || b[0] == 169) continue;
                char buf[64]{};
                snprintf(buf, sizeof(buf), "http://%u.%u.%u.%u:8765", b[0], b[1], b[2], b[3]);
                out = buf;
                break;
            }
            freeaddrinfo(result);
        }
    }
    WSACleanup();
    return out;
}

static void AddUniqueAddress(std::vector<std::string>& history, const std::string& address) {
    if (address.empty()) return;
    if (std::find(history.begin(), history.end(), address) == history.end()) history.push_back(address);
    while (history.size() > 12) history.erase(history.begin());
}

static bool SameDeviceIdentity(const FleetDeviceRow& a, const FleetDeviceRow& b) {
    return (!a.device_id.empty() && a.device_id == b.device_id) ||
           (!a.mac_hash.empty() && a.mac_hash == b.mac_hash) ||
           (!a.sensor_hash.empty() && a.sensor_hash == b.sensor_hash) ||
           (!a.address.empty() && a.address == b.address);
}

static void MergeDeviceRecord(FleetDeviceRow& existing, FleetDeviceRow incoming) {
    const bool trusted = existing.trusted;
    const bool local = existing.local;
    AddUniqueAddress(existing.address_history, existing.address);
    AddUniqueAddress(existing.address_history, incoming.address);
    incoming.address_history = existing.address_history;
    incoming.trusted = trusted;
    incoming.local = local;
    if (incoming.device_id.empty()) incoming.device_id = existing.device_id;
    if (incoming.hostname.empty()) incoming.hostname = existing.hostname;
    if (incoming.last_seen_address.empty()) incoming.last_seen_address = incoming.address;
    if (incoming.last_seen_at_ms <= 0) incoming.last_seen_at_ms = NowMs();
    existing = std::move(incoming);
}

struct ManualAddState {
    HWND owner = nullptr;
    HWND hwnd = nullptr;
    HWND octets[4]{};
    HWND port = nullptr;
    bool done = false;
    bool accepted = false;
    std::string result;
};

static bool ParseIpv4HostPort(const std::string& text, int octets[4], int& port) {
    std::string hostport = StripUrlHost(text);
    port = 8765;
    size_t colon = hostport.rfind(':');
    if (colon != std::string::npos && colon + 1 < hostport.size()) {
        try { port = std::clamp(std::stoi(hostport.substr(colon + 1)), 1, 65535); } catch (...) { port = 8765; }
        hostport = hostport.substr(0, colon);
    }
    int a = -1, b = -1, c = -1, d = -1;
    char tail = 0;
    if (sscanf_s(hostport.c_str(), "%d.%d.%d.%d%c", &a, &b, &c, &d, &tail, 1) != 4) return false;
    int vals[4] = {a, b, c, d};
    for (int i = 0; i < 4; ++i) {
        if (vals[i] < 0 || vals[i] > 255) return false;
        octets[i] = vals[i];
    }
    return true;
}

static LRESULT CALLBACK ManualAddWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* state = reinterpret_cast<ManualAddState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return TRUE;
    }
    case WM_CREATE: {
        state = reinterpret_cast<ManualAddState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        auto add = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) -> HWND {
            HWND child = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                x, y, w, h, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                GetModuleHandleW(nullptr), nullptr);
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            return child;
        };
        add(L"STATIC", L"Enter the sensor IP address and port shown on the remote device.", 0, 18, 18, 448, 20, 0);
        add(L"STATIC", L"IP address", 0, 18, 58, 82, 20, 0);
        int x = 108;
        for (int i = 0; i < 4; ++i) {
            state->octets[i] = add(L"EDIT", L"", WS_BORDER | ES_NUMBER | ES_CENTER | WS_TABSTOP, x, 54, 48, 26, 101 + i);
            SendMessageW(state->octets[i], EM_LIMITTEXT, 3, 0);
            x += 54;
            if (i < 3) add(L"STATIC", L".", 0, x - 6, 60, 10, 20, 0);
        }
        add(L"STATIC", L"Port", 0, 348, 58, 36, 20, 0);
        state->port = add(L"EDIT", L"8765", WS_BORDER | ES_NUMBER | ES_CENTER | WS_TABSTOP, 388, 54, 64, 26, 105);
        SendMessageW(state->port, EM_LIMITTEXT, 5, 0);
        add(L"STATIC", L"OK probes /api/v1/enrollment/readiness.", 0, 18, 98, 448, 18, 0);
        add(L"STATIC", L"Discovery creates an untrusted candidate only.", 0, 18, 118, 448, 18, 0);
        add(L"BUTTON", L"OK", BS_DEFPUSHBUTTON | WS_TABSTOP, 278, 154, 86, 30, IDOK);
        add(L"BUTTON", L"Cancel", WS_TABSTOP, 376, 154, 86, 30, IDCANCEL);
        SetFocus(state->octets[0]);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK && state) {
            int vals[4]{};
            wchar_t text[16]{};
            for (int i = 0; i < 4; ++i) {
                GetWindowTextW(state->octets[i], text, 16);
                vals[i] = _wtoi(text);
                if (vals[i] < 0 || vals[i] > 255 || text[0] == 0) {
                    MessageBoxW(hwnd, L"Enter a valid IPv4 address. Each octet must be 0 through 255.", L"TelemetryApp Manual Add", MB_OK | MB_ICONWARNING);
                    SetFocus(state->octets[i]);
                    return 0;
                }
            }
            GetWindowTextW(state->port, text, 16);
            int port = _wtoi(text);
            if (port <= 0 || port > 65535) {
                MessageBoxW(hwnd, L"Enter a valid TCP port from 1 through 65535.", L"TelemetryApp Manual Add", MB_OK | MB_ICONWARNING);
                SetFocus(state->port);
                return 0;
            }
            char out[64]{};
            snprintf(out, sizeof(out), "%d.%d.%d.%d:%d", vals[0], vals[1], vals[2], vals[3], port);
            state->result = out;
            state->accepted = true;
            state->done = true;
            DestroyWindow(hwnd);
            return 0;
        }
        if (LOWORD(wp) == IDCANCEL && state) {
            state->done = true;
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        if (state) state->done = true;
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static bool PromptManualAddress(HWND owner, std::string& out) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ManualAddWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"TelemetryManualAddDialog";
        RegisterClassExW(&wc);
        registered = true;
    }

    ManualAddState state{};
    state.owner = owner;
    RECT parent{};
    if (owner) GetWindowRect(owner, &parent);
    int width = 490, height = 240;
    int x = parent.left + ((parent.right - parent.left) - width) / 2;
    int y = parent.top + ((parent.bottom - parent.top) - height) / 2;
    if (!owner) { x = CW_USEDEFAULT; y = CW_USEDEFAULT; }

    state.hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME,
        L"TelemetryManualAddDialog", L"Manual Add Sensor",
        WS_CAPTION | WS_SYSMENU | WS_POPUP,
        x, y, width, height, owner, nullptr, GetModuleHandleW(nullptr), &state);
    if (!state.hwnd) return false;

    int vals[4]{};
    int port = 8765;
    std::string clip;
    if (ClipboardText(clip) && ParseIpv4HostPort(clip, vals, port)) {
        wchar_t buf[16]{};
        for (int i = 0; i < 4; ++i) {
            swprintf_s(buf, L"%d", vals[i]);
            SetWindowTextW(state.octets[i], buf);
        }
        swprintf_s(buf, L"%d", port);
        SetWindowTextW(state.port, buf);
    }

    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(state.hwnd, SW_SHOW);
    UpdateWindow(state.hwnd);

    MSG msg{};
    while (!state.done && GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(state.hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (owner) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    out = state.result;
    return state.accepted;
}

static void PumpUiMessagesDuringProbe() {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static bool ProbeReadiness(const std::string& address, FleetDeviceRow& row, std::string& error,
                           int connect_timeout_ms = 1200,
                           int read_timeout_ms = 2500,
                           int write_timeout_ms = 1200) {
    std::string hostport = StripUrlHost(address);
    if (hostport.empty()) {
        error = "No host or IP was provided.";
        return false;
    }
    std::string host = hostport;
    int port = 8765;
    size_t colon = hostport.rfind(':');
    if (colon != std::string::npos && colon + 1 < hostport.size()) {
        host = hostport.substr(0, colon);
        try { port = std::stoi(hostport.substr(colon + 1)); } catch (...) { port = 8765; }
    }

    auto timeout_sec = [](int ms) -> time_t { return static_cast<time_t>(std::max(0, ms) / 1000); };
    auto timeout_usec = [](int ms) -> time_t { return static_cast<time_t>((std::max(0, ms) % 1000) * 1000); };

    httplib::Client cli(host, port);
    cli.set_connection_timeout(timeout_sec(connect_timeout_ms), timeout_usec(connect_timeout_ms));
    cli.set_read_timeout(timeout_sec(read_timeout_ms), timeout_usec(read_timeout_ms));
    cli.set_write_timeout(timeout_sec(write_timeout_ms), timeout_usec(write_timeout_ms));
    auto res = cli.Get("/api/v1/enrollment/readiness");
    if (!res) {
        error = "No response from " + host + ":" + std::to_string(port) +
                " before the " + std::to_string(connect_timeout_ms + read_timeout_ms) +
                " ms readiness timeout. The sensor may be blocked by firewall, stopped, on another subnet, or slower to answer.";
        return false;
    }
    if (res->status != 200) {
        error = "Readiness probe returned HTTP " + std::to_string(res->status) + ".";
        return false;
    }
    json j = json::parse(res->body, nullptr, false);
    if (!j.is_object() || j.value("product", "") != "TelemetryApp") {
        error = "The endpoint responded, but it is not a TelemetryApp readiness endpoint.";
        return false;
    }

    std::string sensor_hash = j.value("sensor_id_hash", "");
    std::string device_id = j.value("device_id", sensor_hash);
    std::string hostname = j.value("hostname", "");
    std::string mac_hash = j.value("mac_hash", "");
    std::string mode = j.value("install_mode", "SensorClient");
    row.name = hostname.empty() ? (sensor_hash.empty() ? host : ("Sensor " + sensor_hash)) : hostname;
    row.role = mode;
    row.address = host + ":" + std::to_string(port);
    row.hostname = hostname;
    row.os = "Windows";
    row.state = "Online";
    row.device_id = device_id;
    row.sensor_hash = sensor_hash;
    row.mac_hash = mac_hash;
    row.last_seen_address = row.address;
    row.last_seen_at_ms = NowMs();
    AddUniqueAddress(row.address_history, row.address);
    row.enrollment_state = j.value("enrollment_state", "");
    row.last_error.clear();
    row.trusted = false;
    row.local = false;
    return true;
}

static bool PostLabEnrollment(const FleetDeviceRow& row, FleetDeviceRow& enrolled, std::string& error) {
    std::string hostport = StripUrlHost(row.address);
    std::string host = hostport;
    int port = 8765;
    size_t colon = hostport.rfind(':');
    if (colon != std::string::npos && colon + 1 < hostport.size()) {
        host = hostport.substr(0, colon);
        try { port = std::stoi(hostport.substr(colon + 1)); } catch (...) { port = 8765; }
    }
    if (host.empty() || row.sensor_hash.empty()) {
        error = "Enrollment needs a reachable address and sensor fingerprint.";
        return false;
    }

    httplib::Client cli(host, port);
    cli.set_connection_timeout(2, 0);
    cli.set_read_timeout(4, 0);
    cli.set_write_timeout(2, 0);

    json body;
    body["accept_lab_enrollment"] = true;
    body["sensor_id_hash"] = row.sensor_hash;
    body["mac_hash"] = row.mac_hash;
    body["host_name"] = LocalComputerNameA();
    body["host_instance"] = "fleet-host-" + LocalComputerNameA();
    body["host_address"] = PreferredFleetHostUrl();

    auto res = cli.Post("/api/v1/enrollment/request", body.dump(), "application/json");
    if (!res) {
        error = "No response to enrollment request. Confirm sensor service, firewall, and TCP 8765 reachability.";
        return false;
    }
    json response = json::parse(res->body, nullptr, false);
    if (res->status < 200 || res->status >= 300 || !response.is_object() || !response.value("accepted", false)) {
        error = response.is_object()
            ? response.value("message", "Enrollment request was rejected.")
            : ("Enrollment returned HTTP " + std::to_string(res->status) + ".");
        return false;
    }

    enrolled = row;
    enrolled.name = response.value("hostname", enrolled.name);
    enrolled.role = response.value("install_mode", enrolled.role);
    enrolled.device_id = response.value("device_id", enrolled.device_id);
    enrolled.sensor_hash = response.value("sensor_id_hash", enrolled.sensor_hash);
    enrolled.mac_hash = response.value("mac_hash", enrolled.mac_hash);
    enrolled.enrollment_state = response.value("enrollment_state", "enrolled_lab");
    enrolled.state = "Online";
    enrolled.trusted = true;
    enrolled.last_seen_address = enrolled.address;
    enrolled.last_seen_at_ms = NowMs();
    AddUniqueAddress(enrolled.address_history, enrolled.address);
    enrolled.last_error.clear();
    return true;
}

static bool SplitHostPort(const std::string& address, std::string& host, int& port, std::string& error) {
    std::string hostport = StripUrlHost(address);
    host = hostport;
    port = 8765;
    size_t colon = hostport.rfind(':');
    if (colon != std::string::npos && colon + 1 < hostport.size()) {
        host = hostport.substr(0, colon);
        try { port = std::stoi(hostport.substr(colon + 1)); } catch (...) { port = 8765; }
    }
    if (host.empty() || port <= 0 || port > 65535) {
        error = "Invalid host or port.";
        return false;
    }
    return true;
}

static bool StartRemoteLabSession(const FleetDeviceRow& row,
                                  const FleetLoggingJob& job,
                                  std::string& session_ref,
                                  std::string& message) {
    session_ref.clear();
    message.clear();
    std::string host;
    int port = 8765;
    if (!SplitHostPort(row.address, host, port, message)) return false;

    httplib::Client cli(host, port);
    cli.set_connection_timeout(2, 0);
    cli.set_read_timeout(5, 0);
    cli.set_write_timeout(2, 0);

    json body;
    body["label"] = job.label + " remote " + row.name;
    body["metric_ids"] = json::array();
    // Do not send the host log folder. The remote sensor writes to its own
    // service data path unless a remote-local absolute folder is configured.
    body["log_dir"] = "";
    if (job.schedule_kind == "OneTime" || job.schedule_kind == "Recurring") {
        body["stop_policy"] = {{"mode", "manual"}};
    }

    auto res = cli.Post("/api/v1/lab/sessions", body.dump(), "application/json");
    if (!res) {
        message = "No response from " + row.address + " while starting remote lab logging.";
        return false;
    }
    json j = json::parse(res->body, nullptr, false);
    if (res->status < 200 || res->status >= 300 || !j.is_object()) {
        message = j.is_object() ? j.value("message", j.value("error", "Remote lab logging rejected."))
                                : ("Remote lab logging returned HTTP " + std::to_string(res->status) + ".");
        return false;
    }
    std::string sid = j.value("session_id", "");
    if (sid.empty()) {
        message = "Remote lab logging response did not include a session_id.";
        return false;
    }
    session_ref = row.address + "|" + sid;
    message = row.name + " -> " + sid + " (" + j.value("log_path", "remote default log path") + ")";
    return true;
}

static bool StopRemoteLabSessionRef(const std::string& session_ref, std::string& message) {
    size_t bar = session_ref.find('|');
    if (bar == std::string::npos || bar == 0 || bar + 1 >= session_ref.size()) {
        message = "Invalid remote session reference.";
        return false;
    }
    std::string address = session_ref.substr(0, bar);
    std::string sid = session_ref.substr(bar + 1);
    std::string host;
    int port = 8765;
    if (!SplitHostPort(address, host, port, message)) return false;

    httplib::Client cli(host, port);
    cli.set_connection_timeout(2, 0);
    cli.set_read_timeout(5, 0);
    cli.set_write_timeout(2, 0);
    auto res = cli.Post(("/api/v1/lab/sessions/" + sid + "/stop").c_str(), "{}", "application/json");
    if (!res) {
        message = "No response while stopping " + sid + " on " + address + ".";
        return false;
    }
    if (res->status < 200 || res->status >= 300) {
        json j = json::parse(res->body, nullptr, false);
        message = j.is_object() ? j.value("message", j.value("error", "Remote stop failed."))
                                : ("Remote stop returned HTTP " + std::to_string(res->status) + ".");
        return false;
    }
    message = "Stopped " + sid + " on " + address + ".";
    return true;
}

static bool FetchRemoteLabSnapshotSummary(const FleetDeviceRow& row, std::string& summary, std::string& error) {
    std::string host;
    int port = 8765;
    if (!SplitHostPort(row.address, host, port, error)) return false;

    httplib::Client cli(host, port);
    cli.set_connection_timeout(2, 0);
    cli.set_read_timeout(4, 0);
    cli.set_write_timeout(2, 0);
    auto res = cli.Get("/api/v1/lab/snapshot");
    if (!res) {
        error = "No response from " + row.address + " while reading remote lab snapshot.";
        return false;
    }
    json j = json::parse(res->body, nullptr, false);
    if (res->status < 200 || res->status >= 300 || !j.is_object()) {
        error = j.is_object() ? j.value("message", j.value("error", "Remote snapshot rejected."))
                              : ("Remote snapshot returned HTTP " + std::to_string(res->status) + ".");
        return false;
    }

    double cpu = j.value("cpu", json::object()).value("usage_total_pct", 0.0);
    double ram = j.value("memory", json::object()).value("percent", 0.0);
    double ram_gb = j.value("memory", json::object()).value("used_gb", 0.0);
    double gpu = 0.0;
    double vram = 0.0;
    if (j.contains("gpus") && j["gpus"].is_array() && !j["gpus"].empty()) {
        gpu = j["gpus"][0].value("usage_pct", 0.0);
        vram = j["gpus"][0].value("vram_pct", 0.0);
    }
    char buf[384]{};
    snprintf(buf, sizeof(buf),
             "%s live telemetry: CPU %.1f%% | RAM %.1f%% (%.2f GB) | GPU %.1f%% | VRAM %.1f%%",
             row.name.c_str(), cpu, ram, ram_gb, gpu, vram);
    summary = buf;
    return true;
}

static std::vector<std::string> LocalIpv4Prefixes() {
    std::vector<std::string> prefixes;
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return prefixes;
    std::set<std::string> unique;
    char hostname[256]{};
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* result = nullptr;
        if (getaddrinfo(hostname, nullptr, &hints, &result) == 0) {
            for (addrinfo* it = result; it; it = it->ai_next) {
                auto* sa = reinterpret_cast<sockaddr_in*>(it->ai_addr);
                if (!sa) continue;
                unsigned char* b = reinterpret_cast<unsigned char*>(&sa->sin_addr.S_un.S_addr);
                if (b[0] == 127 || b[0] == 169) continue;
                char prefix[32]{};
                snprintf(prefix, sizeof(prefix), "%u.%u.%u.", b[0], b[1], b[2]);
                unique.insert(prefix);
            }
            freeaddrinfo(result);
        }
    }
    WSACleanup();
    prefixes.assign(unique.begin(), unique.end());
    return prefixes;
}

static std::string NewJobId() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char buf[64]{};
    snprintf(buf, sizeof(buf), "flog-%04u%02u%02u-%02u%02u%02u",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

static std::string TodayIso(int add_days = 0) {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    std::tm tmv{};
    tmv.tm_year = st.wYear - 1900;
    tmv.tm_mon = st.wMonth - 1;
    tmv.tm_mday = st.wDay + add_days;
    std::mktime(&tmv);
    char buf[24]{};
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday);
    return buf;
}

static std::string FormatBytes(uint64_t bytes) {
    const double gb = 1024.0 * 1024.0 * 1024.0;
    const double mb = 1024.0 * 1024.0;
    char buf[64]{};
    if (bytes >= (uint64_t)gb) snprintf(buf, sizeof(buf), "%.2f GB", bytes / gb);
    else snprintf(buf, sizeof(buf), "%.2f MB", bytes / mb);
    return buf;
}

static uint64_t DirectorySizeBytes(const std::string& path) {
    if (path.empty()) return 0;
    uint64_t total = 0;
    std::string search = path;
    if (!search.empty() && search.back() != '\\' && search.back() != '/') search += "\\";
    WIN32_FIND_DATAA data{};
    HANDLE h = FindFirstFileA((search + "*").c_str(), &data);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        std::string name = data.cFileName;
        if (name == "." || name == "..") continue;
        std::string full = search + name;
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            total += DirectorySizeBytes(full);
        } else {
            ULARGE_INTEGER sz{};
            sz.HighPart = data.nFileSizeHigh;
            sz.LowPart = data.nFileSizeLow;
            total += (uint64_t)sz.QuadPart;
        }
    } while (FindNextFileA(h, &data));
    FindClose(h);
    return total;
}

static bool DirectoryExistsA(const std::string& path) {
    if (path.empty()) return false;
    DWORD attr = GetFileAttributesA(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

static uint64_t DirectorySizeBytesSafe(const std::string& path) {
    if (!DirectoryExistsA(path)) {
        return 0;
    }
    return DirectorySizeBytes(path);
}

static std::string FreeSpaceText(const std::string& path) {
    if (path.empty()) return "free space unknown";
    ULARGE_INTEGER free_avail{}, total{}, total_free{};
    std::string root = path;
    if (root.size() >= 3 && root[1] == ':') root = root.substr(0, 3);
    if (!GetDiskFreeSpaceExA(root.c_str(), &free_avail, &total, &total_free)) {
        return "free space unknown";
    }
    return FormatBytes((uint64_t)free_avail.QuadPart) + " free on disk";
}

static std::wstring HourLabel(int hour) {
    char buf[16]{};
    snprintf(buf, sizeof(buf), "%02d:00", std::clamp(hour, 0, 24));
    return Widen(buf);
}

static std::string HourLabelA(int hour) {
    char buf[16]{};
    snprintf(buf, sizeof(buf), "%02d:00", std::clamp(hour, 0, 24));
    return buf;
}

static void DrawCenteredText(D2DContext& ctx, const wchar_t* label, D2D1_RECT_F rect,
                             D2D1_COLOR_F color, float size_pt, bool bold = true) {
    if (!label || !*label) return;
    IDWriteTextFormat* fmt = ctx.MakeTextFormat(size_pt, bold, true);
    if (!fmt) return;
    ctx.br_scratch->SetColor(color);
    ctx.DC()->DrawTextW(label, static_cast<UINT32>(wcslen(label)), fmt, rect,
        ctx.br_scratch.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    fmt->Release();
}

FleetPage::FleetPage(D2DContext& ctx) : m_ctx(ctx) {
    char name[MAX_COMPUTERNAME_LENGTH + 1]{};
    DWORD len = sizeof(name);
    if (GetComputerNameA(name, &len)) SetLocalHost(name);
    else SetLocalHost("Local Device");
}

void FleetPage::SetStoragePath(const std::string& path) {
    if (m_jobs_path == path) return;
    m_jobs_path = path;
    m_devices_path = path;
    const std::string jobs_file = "fleet_logging_jobs.json";
    size_t pos = m_devices_path.rfind(jobs_file);
    if (pos != std::string::npos) {
        m_devices_path.replace(pos, jobs_file.size(), "fleet_devices.json");
    } else {
        m_devices_path += ".devices.json";
    }
    LoadJobs();
    LoadDevices();
}

void FleetPage::SetLocalHost(const std::string& hostname) {
    m_devices.clear();
    FleetDeviceRow local;
    local.name = hostname.empty() ? "Local Device" : hostname;
    local.role = "FullHost";
    local.address = "localhost:8765";
    local.hostname = local.name;
    local.os = "Windows";
    local.state = "Online";
    local.last_seen_address = local.address;
    local.last_seen_at_ms = NowMs();
    local.trusted = true;
    local.local = true;
    m_devices.push_back(local);
}

void FleetPage::DrawButton(float x, float y, float w, float h, const wchar_t* label,
                           D2D1_COLOR_F color, int id) {
    D2D1_RECT_F r = {x, y, x + w, y + h};
    m_ctx.DC()->FillRoundedRectangle(D2D1::RoundedRect(r, 5, 5), m_ctx.BrushSolid(color));
    m_ctx.DC()->DrawRoundedRectangle(D2D1::RoundedRect(r, 5, 5), m_ctx.BrushSolid(kBorder), 1.0f);
    DrawCenteredText(m_ctx, label, {r.left + 8.0f, r.top, r.right - 8.0f, r.bottom},
        {1, 1, 1, 1}, 10.0f, true);
    m_buttons.push_back({x, y, x + w, y + h, id});
}

void FleetPage::DrawPill(float x, float y, float w, const wchar_t* label, D2D1_COLOR_F color) {
    D2D1_RECT_F r = {x, y, x + w, y + 22.0f};
    D2D1_COLOR_F fill = {color.r * 0.22f, color.g * 0.22f, color.b * 0.22f, 1.0f};
    m_ctx.DC()->FillRoundedRectangle(D2D1::RoundedRect(r, 11, 11), m_ctx.BrushSolid(fill));
    m_ctx.DC()->DrawRoundedRectangle(D2D1::RoundedRect(r, 11, 11), m_ctx.BrushSolid(color), 1.0f);
    DrawCenteredText(m_ctx, label, {r.left + 8.0f, r.top, r.right - 8.0f, r.bottom}, color, 9.0f, true);
}

void FleetPage::DrawDeviceRow(float x, float& y, float w, const FleetDeviceRow& row) {
    size_t index = 0;
    for (size_t i = 0; i < m_devices.size(); ++i) {
        if (&m_devices[i] == &row) { index = i; break; }
    }
    D2D1_RECT_F r = {x + 20, y, x + w - 20, y + 58.0f};
    m_ctx.DC()->FillRoundedRectangle(D2D1::RoundedRect(r, 6, 6), m_ctx.BrushSolid(kPanel));
    m_ctx.DC()->DrawRoundedRectangle(D2D1::RoundedRect(r, 6, 6), m_ctx.BrushSolid(kBorder), 1.0f);

    std::string sub = row.role;
    if (!row.mac_hash.empty()) sub += " | MAC hash " + row.mac_hash;
    else if (!row.sensor_hash.empty()) sub += " | ID " + row.sensor_hash;
    m_ctx.DrawText(Widen(row.name).c_str(), {r.left + 14, y + 8, r.left + 250, y + 28}, kText, 12.0f, true);
    m_ctx.DrawText(Widen(sub).c_str(), {r.left + 14, y + 30, r.left + 350, y + 48}, kDim, 9.5f);
    m_ctx.DrawText(Widen(row.address).c_str(), {r.left + 260, y + 12, r.left + 480, y + 32}, kText, 11.0f);
    m_ctx.DrawText(Widen(row.os).c_str(), {r.left + 500, y + 12, r.left + 650, y + 32}, kDim, 10.0f);

    DrawPill(r.right - 520, y + 18, 86, row.trusted ? L"Trusted" : L"Candidate", row.trusted ? kGreen : kAmber);
    DrawPill(r.right - 426, y + 18, 76, row.state == "Online" ? L"Online" : L"Offline", row.state == "Online" ? kGreen : kRed);
    if (row.local) {
        DrawButton(r.right - 156, y + 14, 62, 28, L"View", kBlue, BTN_VIEW_LOCAL);
    } else {
        DrawButton(r.right - 346, y + 14, 62, 28, L"View", row.trusted ? kBlue : kPanel2, BTN_DEVICE_VIEW_BASE + (int)index);
        DrawButton(r.right - 276, y + 14, 72, 28, L"Refresh", {0.18f,0.24f,0.36f,1}, BTN_DEVICE_REFRESH_BASE + (int)index);
        if (!row.trusted) {
            DrawButton(r.right - 196, y + 14, 70, 28, L"Enroll", kAmber, BTN_DEVICE_ENROLL_BASE + (int)index);
        }
        DrawButton(r.right - 76, y + 14, 60, 28, L"Delete", kRed, BTN_DEVICE_DELETE_BASE + (int)index);
    }
    y += 66.0f;
}

void FleetPage::DrawJobs(float x, float& y, float w) {
    m_ctx.DrawText(L"Fleet Logging Jobs", {x + 20, y, x + w - 200, y + 24}, kText, 15.0f, true);
    DrawButton(x + w - 174, y - 2, 154, 28, L"New Logging Job", kBlue, BTN_NEW_JOB);
    y += 34.0f;

    D2D1_RECT_F intro = {x + 20, y, x + w - 20, y + 68.0f};
    m_ctx.DC()->FillRoundedRectangle(D2D1::RoundedRect(intro, 6, 6), m_ctx.BrushSolid(kPanel2));
    m_ctx.DC()->DrawRoundedRectangle(D2D1::RoundedRect(intro, 6, 6), m_ctx.BrushSolid(kBorder), 1.0f);
    m_ctx.DrawText(L"Guided fleet logging creates auditable jobs with target devices, cadence, storage lifecycle, format, metric scope, validation, and live status.",
        {intro.left + 14, y + 12, intro.right - 14, y + 32}, kDim, 10.5f);
    m_ctx.DrawText(L"Current release runs local/trusted local jobs. Remote jobs remain queued until secure enrollment and host-to-sensor dispatch are enabled.",
        {intro.left + 14, y + 36, intro.right - 14, y + 58}, kAmber, 10.0f, true);
    y += 82.0f;

    if (m_jobs.empty()) {
        D2D1_RECT_F empty = {x + 20, y, x + w - 20, y + 54.0f};
        m_ctx.DC()->FillRoundedRectangle(D2D1::RoundedRect(empty, 6, 6), m_ctx.BrushSolid(kPanel));
        m_ctx.DC()->DrawRoundedRectangle(D2D1::RoundedRect(empty, 6, 6), m_ctx.BrushSolid(kBorder), 1.0f);
        m_ctx.DrawText(L"No fleet logging jobs yet. Create one to define targets, cadence, storage, format, and validation before capture starts.",
            {empty.left + 14, y + 16, empty.right - 14, y + 42}, kDim, 10.5f);
        y += 68.0f;
        return;
    }

    for (size_t i = 0; i < m_jobs.size(); ++i) {
        const auto& job = m_jobs[i];
        float row_h = job.last_error.empty() ? 112.0f : 132.0f;
        D2D1_RECT_F r = {x + 20, y, x + w - 20, y + row_h};
        m_ctx.DC()->FillRoundedRectangle(D2D1::RoundedRect(r, 6, 6), m_ctx.BrushSolid(kPanel));
        m_ctx.DC()->DrawRoundedRectangle(D2D1::RoundedRect(r, 6, 6), m_ctx.BrushSolid(kBorder), 1.0f);
        m_ctx.DrawText(Widen(job.label).c_str(), {r.left + 14, y + 8, r.left + 320, y + 30}, kText, 12.0f, true);
        m_ctx.DrawText(Widen(job.id).c_str(), {r.left + 14, y + 30, r.left + 240, y + 50}, kDim, 9.5f);
        m_ctx.DrawText(Widen(job.target_mode).c_str(), {r.left + 260, y + 12, r.left + 520, y + 32}, kText, 10.0f);
        m_ctx.DrawText(Widen(BuildScheduleLabel(job)).c_str(), {r.left + 260, y + 34, r.left + 620, y + 54}, kDim, 10.0f);
        m_ctx.DrawText(Widen(job.storage_dir).c_str(), {r.left + 14, y + 58, r.right - 220, y + 82}, kDim, 9.5f);
        if (!job.last_error.empty()) {
            m_ctx.DrawText(Widen(job.last_error).c_str(), {r.left + 14, y + 78, r.right - 260, y + 100}, kAmber, 9.5f, true);
        }
        std::string storage = "Current folder size: " + FormatBytes(DirectorySizeBytesSafe(job.storage_dir)) + " | " + FreeSpaceText(job.storage_dir);
        float storage_y = job.last_error.empty() ? y + 82 : y + 102;
        m_ctx.DrawText(Widen(storage).c_str(), {r.left + 14, storage_y, r.right - 220, storage_y + 22}, kBlue, 9.5f, true);

        D2D1_COLOR_F status_col = job.status == "Running" ? kGreen : (job.status == "Failed" ? kRed : kAmber);
        DrawPill(r.right - 330, y + 18, 86, Widen(job.status).c_str(), status_col);
        DrawPill(r.right - 236, y + 18, 74, Widen(job.format).c_str(), kBlue);
        if (job.status == "Running") {
            DrawButton(r.right - 154, y + 14, 66, 28, L"Pause", kAmber, BTN_JOB_STOP_BASE + (int)i);
        } else {
            DrawButton(r.right - 154, y + 14, 66, 28, L"Start", kGreen, BTN_JOB_START_BASE + (int)i);
        }
        DrawButton(r.right - 80, y + 14, 60, 28, L"Delete", kRed, BTN_JOB_DELETE_BASE + (int)i);
        y += row_h + 12.0f;
    }
}

void FleetPage::DrawWizard(float x, float& y, float w) {
    if (!m_wizard_open) return;
    float panel_h = (m_wizard_step == 1) ? 520.0f : 360.0f;
    D2D1_RECT_F panel = {x + 20, y, x + w - 20, y + panel_h};
    m_ctx.DC()->FillRoundedRectangle(D2D1::RoundedRect(panel, 7, 7), m_ctx.BrushSolid({0.11f,0.11f,0.14f,1}));
    m_ctx.DC()->DrawRoundedRectangle(D2D1::RoundedRect(panel, 7, 7), m_ctx.BrushSolid(kBlue), 1.2f);

    const wchar_t* titles[] = {
        L"1. Select Target Devices",
        L"2. Select Logging Cadence",
        L"3. Select Storage and Lifecycle",
        L"4. Select Format and Verbosity",
        L"5. Validate and Create"
    };
    m_ctx.DrawText(L"Fleet Logging Wizard", {panel.left + 16, y + 12, panel.right - 16, y + 36}, kText, 16.0f, true);
    m_ctx.DrawText(titles[std::clamp(m_wizard_step, 0, 4)], {panel.left + 16, y + 42, panel.right - 16, y + 66}, kBlue, 13.0f, true);

    float cy = y + 78.0f;
    auto option = [&](const wchar_t* label, const wchar_t* desc, bool selected, int id) {
        D2D1_RECT_F r = {panel.left + 16, cy, panel.right - 16, cy + 46.0f};
        m_ctx.DC()->FillRoundedRectangle(D2D1::RoundedRect(r, 5, 5), m_ctx.BrushSolid(selected ? D2D1_COLOR_F{0.11f,0.24f,0.36f,1} : kPanel));
        m_ctx.DC()->DrawRoundedRectangle(D2D1::RoundedRect(r, 5, 5), m_ctx.BrushSolid(selected ? kBlue : kBorder), 1.0f);
        m_ctx.DrawText(label, {r.left + 12, cy + 6, r.right - 12, cy + 24}, selected ? kText : kDim, 11.0f, true);
        m_ctx.DrawText(desc, {r.left + 12, cy + 24, r.right - 12, cy + 42}, kDim, 9.5f);
        m_buttons.push_back({r.left, r.top, r.right, r.bottom, id});
        cy += 52.0f;
    };

    if (m_wizard_step == 0) {
        option(L"Local trusted device", L"Capture this machine through the existing local service and session logger.", m_draft.target_mode == "Local trusted device", BTN_OPT_BASE + 1);
        option(L"All trusted fleet devices", L"Starts local capture now; remote sensors stay queued until credentialed dispatch is enabled.", m_draft.target_mode == "All trusted fleet devices", BTN_OPT_BASE + 2);
        option(L"Selected devices", L"Future remote selection model. Current build validates but marks non-local targets queued.", m_draft.target_mode == "Selected devices", BTN_OPT_BASE + 3);
    } else if (m_wizard_step == 1) {
        option(L"Manual start / manual stop", L"Operator starts and stops the job from Fleet Logging Jobs.", m_draft.schedule_kind == "Manual", BTN_OPT_BASE + 10);
        option(L"Always on while online", L"Runs whenever the target machine is on and detected by the host.", m_draft.schedule_kind == "Always", BTN_OPT_BASE + 11);
        option(L"Recurring calendar window", L"Select recurrence, days, all-day behavior, and start/end hours.", m_draft.schedule_kind == "Recurring", BTN_OPT_BASE + 12);
        option(L"One-time scheduled run", L"Select a specific date with all-day or a start/end hour window.", m_draft.schedule_kind == "OneTime", BTN_OPT_BASE + 13);

        if (m_draft.schedule_kind == "Recurring" || m_draft.schedule_kind == "OneTime") {
            auto chip = [&](float bx, float by, float bw, const wchar_t* label, bool selected, int id) {
                D2D1_RECT_F r = {bx, by, bx + bw, by + 26.0f};
                m_ctx.DC()->FillRoundedRectangle(D2D1::RoundedRect(r, 5, 5),
                    m_ctx.BrushSolid(selected ? D2D1_COLOR_F{0.12f,0.28f,0.42f,1} : kPanel));
                m_ctx.DC()->DrawRoundedRectangle(D2D1::RoundedRect(r, 5, 5),
                    m_ctx.BrushSolid(selected ? kBlue : kBorder), 1.0f);
                DrawCenteredText(m_ctx, label, {r.left + 6, r.top, r.right - 6, r.bottom},
                    selected ? kText : kDim, 9.0f, true);
                m_buttons.push_back({r.left, r.top, r.right, r.bottom, id});
            };

            cy += 4.0f;
            D2D1_RECT_F cal = {panel.left + 16, cy, panel.right - 16, panel.bottom - 54.0f};
            m_ctx.DC()->FillRoundedRectangle(D2D1::RoundedRect(cal, 6, 6), m_ctx.BrushSolid(kPanel2));
            m_ctx.DC()->DrawRoundedRectangle(D2D1::RoundedRect(cal, 6, 6), m_ctx.BrushSolid(kBorder), 1.0f);
            float sx = cal.left + 14.0f;
            float sy = cal.top + 12.0f;
            m_ctx.DrawText(L"Calendar Window", {sx, sy, cal.right - 14, sy + 20}, kText, 12.0f, true);
            sy += 28.0f;

            if (m_draft.schedule_kind == "Recurring") {
                const wchar_t* rec_labels[] = {L"Daily", L"Weekly", L"Monthly", L"Bi-daily", L"Bi-weekly", L"Bi-monthly"};
                const char* rec_values[] = {"Daily", "Weekly", "Monthly", "Bi-daily", "Bi-weekly", "Bi-monthly"};
                for (int i = 0; i < 6; ++i) {
                    chip(sx + i * 96.0f, sy, 88.0f, rec_labels[i], m_draft.recurrence == rec_values[i], BTN_OPT_BASE + 50 + i);
                }
                sy += 38.0f;
                const wchar_t* days[] = {L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat"};
                for (int i = 0; i < 7; ++i) {
                    bool selected = (m_draft.days_mask & (1 << i)) != 0;
                    chip(sx + i * 68.0f, sy, 60.0f, days[i], selected, BTN_OPT_BASE + 60 + i);
                }
                sy += 40.0f;
            } else {
                m_ctx.DrawText(L"Date", {sx, sy + 4, sx + 70, sy + 26}, kDim, 10.0f, true);
                chip(sx + 76, sy, 96, L"Today", m_draft.one_time_date == TodayIso(0), BTN_OPT_BASE + 80);
                chip(sx + 180, sy, 112, L"Tomorrow", m_draft.one_time_date == TodayIso(1), BTN_OPT_BASE + 81);
                chip(sx + 300, sy, 112, L"+7 days", m_draft.one_time_date == TodayIso(7), BTN_OPT_BASE + 82);
                m_ctx.DrawText(Widen(m_draft.one_time_date.empty() ? TodayIso(0) : m_draft.one_time_date).c_str(),
                    {sx + 430, sy + 4, cal.right - 20, sy + 26}, kText, 10.0f, true);
                sy += 42.0f;
            }

            chip(sx, sy, 96, L"All day", m_draft.all_day, BTN_OPT_BASE + 70);
            sy += 42.0f;
            if (!m_draft.all_day) {
                m_ctx.DrawText(L"Start", {sx, sy + 6, sx + 64, sy + 28}, kDim, 10.0f, true);
                chip(sx + 70, sy, 34, L"-", false, BTN_OPT_BASE + 71);
                chip(sx + 110, sy, 78, HourLabel(m_draft.start_hour).c_str(), true, BTN_OPT_BASE + 99);
                chip(sx + 196, sy, 34, L"+", false, BTN_OPT_BASE + 72);
                m_ctx.DrawText(L"End", {sx + 258, sy + 6, sx + 310, sy + 28}, kDim, 10.0f, true);
                chip(sx + 316, sy, 34, L"-", false, BTN_OPT_BASE + 73);
                chip(sx + 356, sy, 78, HourLabel(m_draft.end_hour).c_str(), true, BTN_OPT_BASE + 99);
                chip(sx + 442, sy, 34, L"+", false, BTN_OPT_BASE + 74);
            } else {
                m_ctx.DrawText(L"All-day selected: captures the entire selected day/window without hour limits.",
                    {sx, sy + 6, cal.right - 14, sy + 30}, kGreen, 10.0f, true);
            }
        }
    } else if (m_wizard_step == 2) {
        option(L"Choose log folder", L"Select where per-device logs and fleet index metadata should be written.", false, BTN_WIZ_PICK_FOLDER);
        m_ctx.DrawText(Widen(m_draft.storage_dir.empty() ? "No folder selected" : m_draft.storage_dir).c_str(),
            {panel.left + 28, cy, panel.right - 28, cy + 24}, m_draft.storage_dir.empty() ? kRed : kGreen, 10.0f, true);
        cy += 34.0f;
        option(L"Daily per-device logs + fleet index", L"Recommended: repairable per-device raw logs plus a searchable fleet master index.", m_draft.partition == "Daily per-device logs + fleet index", BTN_OPT_BASE + 20);
        option(L"Monthly per-device logs + fleet index", L"Lower folder count for low-volume logging jobs.", m_draft.partition == "Monthly per-device logs + fleet index", BTN_OPT_BASE + 21);
    } else if (m_wizard_step == 3) {
        option(L"Balanced JSONL", L"Recommended: schema-friendly JSON Lines with core metrics and metadata.", m_draft.format == "jsonl" && m_draft.verbosity == "Balanced", BTN_OPT_BASE + 30);
        option(L"Storage-friendly CSV", L"Compact flat rows for spreadsheet-style analysis; weaker for nested process/fleet data.", m_draft.format == "csv", BTN_OPT_BASE + 31);
        option(L"Full diagnostic JSONL", L"All selected metrics and richer metadata. Heavier but best for remediation.", m_draft.verbosity == "Full diagnostic", BTN_OPT_BASE + 32);
        option(L"Minimal selected metrics", L"Storage-friendly policy using selected high-signal metrics only.", m_draft.verbosity == "Minimal", BTN_OPT_BASE + 33);
    } else {
        std::string validation = ValidationText();
        m_ctx.DrawText(Widen(validation).c_str(), {panel.left + 16, cy, panel.right - 16, cy + 140}, validation.find("BLOCKED") == std::string::npos ? kGreen : kRed, 10.5f, true);
        cy += 146.0f;
        m_ctx.DrawText(L"Final check: per-device logs are the source of truth; fleet-index metadata records job status, targets, paths, failures, and lifecycle decisions.",
            {panel.left + 16, cy, panel.right - 16, cy + 54}, kDim, 10.0f);
    }

    float fy = panel.bottom - 44.0f;
    DrawButton(panel.left + 16, fy, 88, 28, L"Cancel", {0.24f,0.14f,0.16f,1}, BTN_WIZ_CANCEL);
    if (m_wizard_step > 0) DrawButton(panel.left + 114, fy, 82, 28, L"Back", {0.18f,0.20f,0.28f,1}, BTN_WIZ_BACK);
    if (m_wizard_step < 4) DrawButton(panel.right - 112, fy, 96, 28, L"Next", kBlue, BTN_WIZ_NEXT);
    else DrawButton(panel.right - 132, fy, 116, 28, L"Create Job", kGreen, BTN_WIZ_CREATE);

    y += panel_h + 18.0f;
}

void FleetPage::Draw(float x, float y, float w, float h, float) {
    m_ctx.DC()->FillRectangle({x, y, x + w, y + h}, m_ctx.BrushSolid(kBg));
    m_ctx.DC()->PushAxisAlignedClip({x, y, x + w, y + h}, D2D1_ANTIALIAS_MODE_ALIASED);
    m_buttons.clear();
    m_view_h = h;

    float cy = y + 18.0f - m_scroll_y;
    m_ctx.DrawText(L"Fleet Management", {x + 20, cy, x + w - 20, cy + 34}, kText, 20.0f, true);
    DrawButton(x + w - 482, cy, 154, 28, L"New Logging Job", kBlue, BTN_NEW_JOB);
    DrawButton(x + w - 318, cy, 112, 28, L"Search LAN", kBlue, BTN_SEARCH);
    DrawButton(x + w - 196, cy, 104, 28, L"Manual Add", {0.18f, 0.24f, 0.36f, 1.0f}, BTN_ADD_MANUAL);
    cy += 40.0f;

    m_ctx.DrawText(L"Enterprise fleet access is enrollment-first. Logging jobs are auditable policies; remote execution remains gated by secure trust.",
        {x + 20, cy, x + w - 20, cy + 20}, kDim, 11.0f);
    cy += 32.0f;

    D2D1_RECT_F summary = {x + 20, cy, x + w - 20, cy + 92.0f};
    m_ctx.DC()->FillRoundedRectangle(D2D1::RoundedRect(summary, 6, 6), m_ctx.BrushSolid(kPanel2));
    m_ctx.DC()->DrawRoundedRectangle(D2D1::RoundedRect(summary, 6, 6), m_ctx.BrushSolid(kBorder), 1.0f);
    m_ctx.DrawText(L"Fleet Logging Readiness", {summary.left + 14, cy + 10, summary.right - 20, cy + 30}, kText, 13.0f, true);
    m_ctx.DrawText(L"Local logging jobs are executable now. Remote jobs are policy-ready but require TLS/enrollment, trusted inventory, and sensor acknowledgement.",
        {summary.left + 14, cy + 34, summary.right - 20, cy + 54}, kDim, 10.0f);
    DrawPill(summary.left + 14, cy + 62, 112, m_service_connected ? L"Service Online" : L"Service Offline", m_service_connected ? kGreen : kRed);
    DrawPill(summary.left + 138, cy + 62, 148, L"Local Jobs Ready", kGreen);
    DrawPill(summary.left + 300, cy + 62, 172, L"Remote Dispatch Gated", kAmber);
    cy += 110.0f;

    DrawWizard(x, cy, w);
    DrawJobs(x, cy, w);

    m_ctx.DrawText(L"Devices", {x + 20, cy, x + w - 20, cy + 24}, kText, 14.0f, true);
    cy += 28.0f;
    for (const auto& device : m_devices) DrawDeviceRow(x, cy, w, device);

    cy += 8.0f;
    m_ctx.DrawText(L"Discovery Status", {x + 20, cy, x + w - 20, cy + 24}, kText, 14.0f, true);
    cy += 28.0f;
    D2D1_RECT_F empty = {x + 20, cy, x + w - 20, cy + 82.0f};
    m_ctx.DC()->FillRoundedRectangle(D2D1::RoundedRect(empty, 6, 6), m_ctx.BrushSolid(kPanel));
    m_ctx.DC()->DrawRoundedRectangle(D2D1::RoundedRect(empty, 6, 6), m_ctx.BrushSolid(kBorder), 1.0f);
    std::wstring status = m_discovery_status.empty()
        ? (m_discovery_attempted
            ? L"No remote clients responded. Check sensor service, LAN binding, firewall TCP 8765, and subnet."
            : L"Search LAN probes local subnet readiness endpoints. Manual Add probes a copied host/IP from clipboard.")
        : Widen(m_discovery_status);
    m_ctx.DrawText(status.c_str(), {empty.left + 14, cy + 14, empty.right - 14, cy + 42}, kDim, 11.0f);
    m_ctx.DrawText(L"Discovery creates candidates only. Trust still requires explicit enrollment/API credential handling; no auto-enroll occurs.",
        {empty.left + 14, cy + 46, empty.right - 14, cy + 72}, kDim, 10.0f);
    cy += 100.0f;

    m_content_h = (cy + m_scroll_y) - y + 10.0f;
    ClampScroll();
    DrawScrollbar(x, y, w, h);
    m_ctx.DC()->PopAxisAlignedClip();
}

void FleetPage::OnClick(float cx, float cy) {
    if (m_content_h > m_view_h + 1.0f &&
        cx >= m_scroll_rail_x0 && cx <= m_scroll_rail_x1 &&
        cy >= m_scroll_rail_y0 && cy <= m_scroll_rail_y1) {
        if (cy >= m_scroll_thumb_y0 && cy <= m_scroll_thumb_y1) {
            m_scroll_dragging = true;
            m_scroll_drag_offset = cy - m_scroll_thumb_y0;
        } else {
            ScrollToThumbY(cy);
        }
        return;
    }

    for (const auto& b : m_buttons) {
        if (cx < b.x0 || cx > b.x1 || cy < b.y0 || cy > b.y1) continue;
        if (b.id == BTN_SEARCH) {
            SearchLanCandidates();
            return;
        }
        else if (b.id == BTN_ADD_MANUAL) {
            ManualAddDevice();
            return;
        }
        else if (b.id == BTN_VIEW_LOCAL && m_on_view_local) m_on_view_local();
        else if (b.id == BTN_NEW_JOB) BeginWizard();
        else if (b.id >= BTN_JOB_START_BASE && b.id < BTN_JOB_START_BASE + 200) StartJob((size_t)(b.id - BTN_JOB_START_BASE));
        else if (b.id >= BTN_JOB_STOP_BASE && b.id < BTN_JOB_STOP_BASE + 200) StopJob((size_t)(b.id - BTN_JOB_STOP_BASE));
        else if (b.id >= BTN_JOB_DELETE_BASE && b.id < BTN_JOB_DELETE_BASE + 200) DeleteJob((size_t)(b.id - BTN_JOB_DELETE_BASE));
        else if (b.id >= BTN_DEVICE_ENROLL_BASE && b.id < BTN_DEVICE_ENROLL_BASE + 200) EnrollDevice((size_t)(b.id - BTN_DEVICE_ENROLL_BASE));
        else if (b.id >= BTN_DEVICE_DELETE_BASE && b.id < BTN_DEVICE_DELETE_BASE + 200) DeleteDevice((size_t)(b.id - BTN_DEVICE_DELETE_BASE));
        else if (b.id >= BTN_DEVICE_REFRESH_BASE && b.id < BTN_DEVICE_REFRESH_BASE + 200) RefreshDevice((size_t)(b.id - BTN_DEVICE_REFRESH_BASE));
        else if (b.id >= BTN_DEVICE_VIEW_BASE && b.id < BTN_DEVICE_VIEW_BASE + 200) ViewDevice((size_t)(b.id - BTN_DEVICE_VIEW_BASE));
        else if (b.id == BTN_WIZ_CANCEL) m_wizard_open = false;
        else if (b.id == BTN_WIZ_BACK && m_wizard_step > 0) --m_wizard_step;
        else if (b.id == BTN_WIZ_NEXT && m_wizard_step < 4) ++m_wizard_step;
        else if (b.id == BTN_WIZ_CREATE) CreateDraftJob();
        else if (b.id == BTN_WIZ_PICK_FOLDER && m_on_choose_log_folder) {
            std::string folder;
            if (m_on_choose_log_folder(folder)) m_draft.storage_dir = folder;
        } else if (b.id >= BTN_OPT_BASE) {
            int opt = b.id - BTN_OPT_BASE;
            if (opt == 1) { m_draft.target_mode = "Local trusted device"; m_draft.local_only = true; m_draft.target_count = 1; }
            if (opt == 2) { m_draft.target_mode = "All trusted fleet devices"; m_draft.local_only = false; m_draft.target_count = (int)m_devices.size(); }
            if (opt == 3) { m_draft.target_mode = "Selected devices"; m_draft.local_only = false; m_draft.target_count = 1; }
            if (opt == 10) m_draft.schedule_kind = "Manual";
            if (opt == 11) m_draft.schedule_kind = "Always";
            if (opt == 12) m_draft.schedule_kind = "Recurring";
            if (opt == 13) {
                m_draft.schedule_kind = "OneTime";
                if (m_draft.one_time_date.empty()) m_draft.one_time_date = TodayIso(0);
            }
            if (opt >= 50 && opt <= 55) {
                const char* rec_values[] = {"Daily", "Weekly", "Monthly", "Bi-daily", "Bi-weekly", "Bi-monthly"};
                m_draft.recurrence = rec_values[opt - 50];
            }
            if (opt >= 60 && opt <= 66) {
                int bit = 1 << (opt - 60);
                m_draft.days_mask ^= bit;
                if (m_draft.days_mask == 0) m_draft.days_mask = bit;
            }
            if (opt == 70) m_draft.all_day = !m_draft.all_day;
            if (opt == 71) m_draft.start_hour = std::clamp(m_draft.start_hour - 1, 0, 23);
            if (opt == 72) m_draft.start_hour = std::clamp(m_draft.start_hour + 1, 0, 23);
            if (opt == 73) m_draft.end_hour = std::clamp(m_draft.end_hour - 1, 1, 24);
            if (opt == 74) m_draft.end_hour = std::clamp(m_draft.end_hour + 1, 1, 24);
            if (m_draft.end_hour <= m_draft.start_hour) m_draft.end_hour = std::min(24, m_draft.start_hour + 1);
            if (opt == 80) m_draft.one_time_date = TodayIso(0);
            if (opt == 81) m_draft.one_time_date = TodayIso(1);
            if (opt == 82) m_draft.one_time_date = TodayIso(7);
            if (opt == 20) m_draft.partition = "Daily per-device logs + fleet index";
            if (opt == 21) m_draft.partition = "Monthly per-device logs + fleet index";
            if (opt == 30) { m_draft.format = "jsonl"; m_draft.verbosity = "Balanced"; }
            if (opt == 31) { m_draft.format = "csv"; m_draft.verbosity = "Storage friendly"; }
            if (opt == 32) { m_draft.format = "jsonl"; m_draft.verbosity = "Full diagnostic"; }
            if (opt == 33) { m_draft.format = "jsonl"; m_draft.verbosity = "Minimal"; }
            m_draft.schedule = BuildScheduleLabel();
        }
        return;
    }
}

void FleetPage::BeginWizard() {
    m_draft = FleetLoggingJob{};
    m_draft.id = NewJobId();
    m_draft.label = "Fleet logging " + m_draft.id;
    m_wizard_step = 0;
    m_wizard_open = true;
}

std::string FleetPage::BuildScheduleLabel() const {
    return BuildScheduleLabel(m_draft);
}

std::string FleetPage::BuildScheduleLabel(const FleetLoggingJob& job) const {
    if (job.schedule_kind == "Manual") return "Manual start";
    if (job.schedule_kind == "Always") return "Always on while online";

    auto days = [&]() -> std::string {
        const char* names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
        if ((job.days_mask & 0x7f) == 0x7f) return "Every day";
        if ((job.days_mask & 0x3e) == 0x3e) return "Mon-Fri";
        std::string out;
        for (int i = 0; i < 7; ++i) {
            if ((job.days_mask & (1 << i)) == 0) continue;
            if (!out.empty()) out += "/";
            out += names[i];
        }
        return out.empty() ? "No days" : out;
    };

    std::string hours = job.all_day ? "all day" :
        (HourLabelA(job.start_hour) + "-" + HourLabelA(job.end_hour));

    if (job.schedule_kind == "Recurring") {
        return job.recurrence + " " + days() + " " + hours;
    }
    if (job.schedule_kind == "OneTime") {
        std::string date = job.one_time_date.empty() ? TodayIso(0) : job.one_time_date;
        return "One-time " + date + " " + hours;
    }
    return job.schedule.empty() ? "Manual start" : job.schedule;
}

std::string FleetPage::ValidationText() const {
    std::ostringstream out;
    if (m_draft.storage_dir.empty()) {
        out << "BLOCKED: choose a log folder before creating the job.\n";
    } else if (!m_service_connected && m_draft.local_only) {
        out << "BLOCKED: local service is offline; start the service before live capture.\n";
    } else {
        out << "READY: job can be created. ";
        out << (m_draft.local_only ? "Local execution is available now." : "Local capture can start now; remote sensor dispatch will stay queued until credentialed dispatch is active.");
        out << "\n";
    }
    out << "Targets: " << m_draft.target_mode << " (" << m_draft.target_count << ")\n";
    out << "Cadence: " << BuildScheduleLabel() << "\n";
    out << "Storage: " << (m_draft.storage_dir.empty() ? "<not selected>" : m_draft.storage_dir) << "\n";
    out << "Lifecycle: " << m_draft.partition << "\n";
    out << "Format: " << m_draft.format << " / " << m_draft.verbosity;
    return out.str();
}

void FleetPage::CreateDraftJob() {
    if (ValidationText().find("BLOCKED") != std::string::npos) return;
    m_draft.schedule = BuildScheduleLabel();
    m_draft.status = m_draft.local_only ? "Ready" : "Queued";
    m_jobs.push_back(m_draft);
    SaveJobs();
    m_wizard_open = false;
}

void FleetPage::StartJob(size_t index) {
    if (index >= m_jobs.size()) return;
    auto& job = m_jobs[index];
    bool local_started = false;
    bool remote_attempted = false;
    int remote_started = 0;
    std::vector<std::string> notes;
    job.remote_session_refs.clear();

    if (m_on_start_local_job) {
        std::string error;
        local_started = m_on_start_local_job(job, error);
        if (!local_started) {
            notes.push_back(error.empty() ? "Local logging session failed to start." : error);
        }
    }

    if (!job.local_only) {
        for (const auto& device : m_devices) {
            if (device.local || !device.trusted) continue;
            remote_attempted = true;
            std::string ref;
            std::string msg;
            if (StartRemoteLabSession(device, job, ref, msg)) {
                ++remote_started;
                job.remote_session_refs.push_back(ref);
                notes.push_back(msg);
            } else {
                notes.push_back(device.name + ": " + msg);
            }
        }
        if (!remote_attempted) {
            notes.push_back("No trusted remote sensors were available for remote lab logging.");
        }
    }

    if (local_started || remote_started > 0) {
        if (!m_running_job_id.empty()) {
            for (auto& j : m_jobs) if (j.id == m_running_job_id) j.status = "Stopped";
        }
        job.status = "Running";
        if (job.local_only) {
            job.last_error.clear();
        } else {
            std::ostringstream status;
            status << (local_started ? "Local capture running" : "Local capture not running")
                   << "; remote lab sessions started: " << remote_started << ".";
            if (!notes.empty()) status << " " << notes.front();
            job.last_error = status.str();
        }
        m_running_job_id = job.id;
    } else {
        job.status = "Failed";
        std::ostringstream fail;
        fail << "No logging session started.";
        if (!notes.empty()) fail << " " << notes.front();
        job.last_error = fail.str();
    }
    SaveJobs();
}

void FleetPage::StopJob(size_t index) {
    if (index >= m_jobs.size()) return;
    if (m_on_stop_local_job) m_on_stop_local_job();
    std::vector<std::string> notes;
    for (const auto& ref : m_jobs[index].remote_session_refs) {
        std::string msg;
        StopRemoteLabSessionRef(ref, msg);
        if (!msg.empty()) notes.push_back(msg);
    }
    m_jobs[index].remote_session_refs.clear();
    m_jobs[index].status = "Paused";
    if (!notes.empty()) m_jobs[index].last_error = notes.front();
    if (m_running_job_id == m_jobs[index].id) m_running_job_id.clear();
    SaveJobs();
}

void FleetPage::DeleteJob(size_t index) {
    if (index >= m_jobs.size()) return;
    if (m_running_job_id == m_jobs[index].id && m_on_stop_local_job) m_on_stop_local_job();
    for (const auto& ref : m_jobs[index].remote_session_refs) {
        std::string ignored;
        StopRemoteLabSessionRef(ref, ignored);
    }
    if (m_running_job_id == m_jobs[index].id) m_running_job_id.clear();
    m_jobs.erase(m_jobs.begin() + static_cast<std::ptrdiff_t>(index));
    SaveJobs();
}

void FleetPage::ViewDevice(size_t index) {
    if (index >= m_devices.size()) return;
    const auto& row = m_devices[index];
    if (row.local) {
        if (m_on_view_local) m_on_view_local();
        return;
    }
    if (!row.trusted) {
        m_discovery_status = "Enroll " + row.address + " before opening remote telemetry. Candidate discovery proves reachability only.";
        return;
    }
    if (m_on_view_remote) {
        m_on_view_remote(row);
        return;
    }
    std::string summary;
    std::string error;
    if (FetchRemoteLabSnapshotSummary(row, summary, error)) {
        m_discovery_status = summary + ". Lab snapshot succeeded; full remote dashboard drill-in remains the next UI step.";
        if (index < m_devices.size()) {
            m_devices[index].state = "Online";
            m_devices[index].last_error.clear();
            SaveDevices();
        }
    } else {
        m_discovery_status = "Remote view failed for " + row.address + ": " + error;
        if (index < m_devices.size()) {
            m_devices[index].state = "Offline";
            m_devices[index].last_error = error;
            SaveDevices();
        }
    }
}

void FleetPage::EnrollDevice(size_t index) {
    if (index >= m_devices.size()) return;
    if (m_devices[index].local) return;

    FleetDeviceRow enrolled;
    std::string error;
    if (!PostLabEnrollment(m_devices[index], enrolled, error)) {
        m_devices[index].last_error = error;
        m_discovery_status = "Enrollment failed for " + m_devices[index].address + ": " + error;
        SaveDevices();
        return;
    }

    MergeDeviceRecord(m_devices[index], enrolled);
    m_devices[index].trusted = true;
    m_discovery_status = "Enrolled " + enrolled.name + " at " + enrolled.address +
        " as a trusted lab device. MAC hash is used as the primary duplicate key.";
    SaveDevices();
    if (m_on_devices_changed) m_on_devices_changed();
}

void FleetPage::RefreshDevice(size_t index) {
    if (index >= m_devices.size()) return;
    if (m_devices[index].local) {
        if (m_on_view_local) m_on_view_local();
        return;
    }
    std::string error;
    std::string address = m_devices[index].address;
    FleetDeviceRow before = m_devices[index];
    bool was_trusted = m_devices[index].trusted;
    if (AddOrUpdateRemoteCandidate(address, error, 1500, 3000)) {
        for (auto& d : m_devices) {
            if (SameDeviceIdentity(d, before)) d.trusted = d.trusted || was_trusted;
        }
        m_discovery_status = "Refreshed " + address + ". Device is online; enrollment status is unchanged.";
        SaveDevices();
    } else {
        m_devices[index].state = "Offline";
        m_devices[index].last_error = error;
        m_discovery_status = "Refresh failed for " + address + ": " + error;
        SaveDevices();
    }
}

void FleetPage::DeleteDevice(size_t index) {
    if (index >= m_devices.size()) return;
    if (m_devices[index].local) return;
    m_discovery_status = "Removed " + m_devices[index].address + " from Devices. Search LAN or Manual Add can rediscover it.";
    m_devices.erase(m_devices.begin() + static_cast<std::ptrdiff_t>(index));
    SaveDevices();
    if (m_on_devices_changed) m_on_devices_changed();
}

void FleetPage::OnScroll(float delta) {
    m_scroll_y -= delta * 44.0f;
    ClampScroll();
}

void FleetPage::OnMouseMove(float, float y) {
    if (!m_scroll_dragging) return;
    ScrollToThumbY(y - m_scroll_drag_offset);
}

void FleetPage::OnMouseUp() {
    m_scroll_dragging = false;
}

void FleetPage::ClampScroll() {
    float max_scroll = std::max(0.0f, m_content_h - m_view_h);
    m_scroll_y = std::clamp(m_scroll_y, 0.0f, max_scroll);
}

void FleetPage::DrawScrollbar(float x, float y, float w, float h) {
    m_scroll_rail_x0 = m_scroll_rail_y0 = m_scroll_rail_x1 = m_scroll_thumb_y0 = m_scroll_thumb_y1 = 0.0f;
    if (m_content_h <= m_view_h + 1.0f) return;
    float rail_x = x + w - 16.0f;
    D2D1_RECT_F rail = {rail_x, y + 8.0f, rail_x + 10.0f, y + h - 8.0f};
    m_scroll_rail_x0 = rail.left; m_scroll_rail_y0 = rail.top; m_scroll_rail_x1 = rail.right; m_scroll_rail_y1 = rail.bottom;
    m_ctx.DC()->FillRoundedRectangle(D2D1::RoundedRect(rail, 5, 5), m_ctx.BrushSolid({0.06f,0.06f,0.08f,1}));
    m_ctx.DC()->DrawRoundedRectangle(D2D1::RoundedRect(rail, 5, 5), m_ctx.BrushSolid({0.34f,0.34f,0.40f,1}), 0.75f);
    float track_h = rail.bottom - rail.top;
    float thumb_h = std::max(44.0f, track_h * (m_view_h / std::max(m_content_h, 1.0f)));
    float thumb_y = rail.top + (track_h - thumb_h) * (m_scroll_y / std::max(1.0f, m_content_h - m_view_h));
    m_scroll_thumb_y0 = thumb_y; m_scroll_thumb_y1 = thumb_y + thumb_h;
    D2D1_RECT_F thumb = {rail.left + 2.0f, thumb_y, rail.right - 2.0f, thumb_y + thumb_h};
    m_ctx.DC()->FillRoundedRectangle(D2D1::RoundedRect(thumb, 4, 4), m_ctx.BrushSolid({0.25f,0.60f,1.0f,0.95f}));
}

void FleetPage::ScrollToThumbY(float y) {
    float track_h = m_scroll_rail_y1 - m_scroll_rail_y0;
    float thumb_h = m_scroll_thumb_y1 - m_scroll_thumb_y0;
    float usable = std::max(1.0f, track_h - thumb_h);
    float clamped = std::clamp(y, m_scroll_rail_y0, m_scroll_rail_y0 + usable);
    m_scroll_y = ((clamped - m_scroll_rail_y0) / usable) * std::max(0.0f, m_content_h - m_view_h);
    ClampScroll();
}

bool FleetPage::AddOrUpdateRemoteCandidate(const std::string& address, std::string& error,
                                           int connect_timeout_ms,
                                           int read_timeout_ms) {
    FleetDeviceRow row;
    if (!ProbeReadiness(address, row, error, connect_timeout_ms, read_timeout_ms, connect_timeout_ms)) return false;

    for (auto& existing : m_devices) {
        if (SameDeviceIdentity(existing, row)) {
            MergeDeviceRecord(existing, row);
            SaveDevices();
            if (m_on_devices_changed) m_on_devices_changed();
            return true;
        }
    }
    m_devices.push_back(row);
    SaveDevices();
    if (m_on_devices_changed) m_on_devices_changed();
    return true;
}

void FleetPage::ManualAddDevice() {
    m_discovery_attempted = true;
    std::string host;
    HWND owner = GetActiveWindow();
    if (!PromptManualAddress(owner, host)) {
        m_discovery_status = "Manual Add cancelled. Enter the sensor IP shown in the remote device's green service indicator.";
        return;
    }

    m_discovery_status = "Manual Add probing " + StripUrlHost(host) + " for enrollment readiness...";
    std::string error;
    if (AddOrUpdateRemoteCandidate(host, error, 1500, 3000)) {
        m_discovery_status = "Manual Add found a TelemetryApp sensor candidate at " + StripUrlHost(host) +
            ". Readiness handshake succeeded; enrollment/trust is still required before remote telemetry view.";
    } else {
        m_discovery_status = "Manual Add failed for " + StripUrlHost(host) + ": " + error;
    }
}

void FleetPage::SearchLanCandidates() {
    m_discovery_attempted = true;
    m_discovery_running = true;
    int found = 0;
    int probed = 0;
    std::string last_error;
    auto prefixes = LocalIpv4Prefixes();
    if (prefixes.empty()) {
        m_discovery_status = "No active IPv4 LAN adapter was found for subnet probing.";
        m_discovery_running = false;
        return;
    }

    for (const auto& prefix : prefixes) {
        for (int host = 1; host <= 254; ++host) {
            PumpUiMessagesDuringProbe();
            std::string addr = prefix + std::to_string(host) + ":8765";
            std::string error;
            ++probed;
            if (AddOrUpdateRemoteCandidate(addr, error, 100, 150)) {
                ++found;
            } else {
                last_error = error;
            }
        }
    }

    if (found > 0) {
        m_discovery_status = "Search LAN found " + std::to_string(found) +
            " TelemetryApp sensor candidate(s). They remain untrusted until explicitly enrolled.";
    } else {
        m_discovery_status = "Search LAN probed " + std::to_string(probed) +
            " address(es), but no sensor responded. Most likely causes: sensor service stopped, Remote API/LAN binding disabled, firewall blocking TCP 8765, or different subnet.";
        if (!last_error.empty()) m_discovery_status += " Last probe: " + last_error;
    }
    m_discovery_running = false;
}

void FleetPage::LoadJobs() {
    m_jobs.clear();
    if (m_jobs_path.empty()) return;
    std::ifstream f(m_jobs_path);
    if (!f.is_open()) return;
    try {
        json root = json::parse(f);
        for (auto& j : root.value("jobs", json::array())) {
            FleetLoggingJob job;
            job.id = j.value("id", "");
            job.label = j.value("label", job.id);
            job.target_mode = j.value("target_mode", "Local trusted device");
            job.schedule = j.value("schedule", "Manual start");
            job.storage_dir = j.value("storage_dir", "");
            job.partition = j.value("partition", "Daily per-device logs + fleet index");
            job.format = j.value("format", "jsonl");
            job.verbosity = j.value("verbosity", "Balanced");
            job.status = j.value("status", "Ready");
            job.last_error = j.value("last_error", "");
            job.schedule_kind = j.value("schedule_kind", job.schedule == "Manual start" ? "Manual" : "Recurring");
            job.recurrence = j.value("recurrence", "Weekly");
            job.one_time_date = j.value("one_time_date", "");
            job.days_mask = j.value("days_mask", 62);
            job.start_hour = j.value("start_hour", 9);
            job.end_hour = j.value("end_hour", 17);
            job.target_count = j.value("target_count", 1);
            job.rows_written = j.value("rows_written", 0);
            job.local_only = j.value("local_only", true);
            job.all_day = j.value("all_day", false);
            job.remote_session_refs.clear();
            if (j.contains("remote_session_refs") && j["remote_session_refs"].is_array()) {
                for (const auto& ref : j["remote_session_refs"]) {
                    if (ref.is_string()) job.remote_session_refs.push_back(ref.get<std::string>());
                }
            }
            job.schedule = BuildScheduleLabel(job);
            if (!job.id.empty()) m_jobs.push_back(job);
        }
    } catch (...) {
        m_jobs.clear();
    }
}

void FleetPage::SaveJobs() const {
    if (m_jobs_path.empty()) return;
    json root;
    root["schema"] = 1;
    root["truth_model"] = "local jobs executable; remote jobs queued until secure enrollment and dispatch are active";
    root["jobs"] = json::array();
    for (const auto& job : m_jobs) {
        root["jobs"].push_back({
            {"id", job.id}, {"label", job.label}, {"target_mode", job.target_mode},
            {"schedule", job.schedule}, {"storage_dir", job.storage_dir},
            {"schedule_kind", job.schedule_kind}, {"recurrence", job.recurrence},
            {"one_time_date", job.one_time_date}, {"days_mask", job.days_mask},
            {"start_hour", job.start_hour}, {"end_hour", job.end_hour},
            {"partition", job.partition}, {"format", job.format}, {"verbosity", job.verbosity},
            {"status", job.status}, {"last_error", job.last_error},
            {"target_count", job.target_count}, {"rows_written", job.rows_written},
            {"local_only", job.local_only}, {"all_day", job.all_day},
            {"remote_session_refs", job.remote_session_refs}
        });
    }
    std::ofstream f(m_jobs_path);
    if (f.is_open()) f << root.dump(2);
}

void FleetPage::LoadDevices() {
    FleetDeviceRow local;
    bool have_local = false;
    for (const auto& d : m_devices) {
        if (d.local) {
            local = d;
            have_local = true;
            break;
        }
    }
    m_devices.clear();
    if (have_local) m_devices.push_back(local);

    if (m_devices_path.empty()) return;
    std::ifstream f(m_devices_path);
    if (!f.is_open()) return;
    try {
        json root = json::parse(f);
        for (const auto& j : root.value("devices", json::array())) {
            FleetDeviceRow row;
            row.name = j.value("name", "");
            row.role = j.value("role", "SensorClient");
            row.address = j.value("address", "");
            row.hostname = j.value("hostname", row.name);
            row.os = j.value("os", "Windows");
            row.state = j.value("state", "Offline");
            row.device_id = j.value("device_id", "");
            row.sensor_hash = j.value("sensor_hash", "");
            row.mac_hash = j.value("mac_hash", "");
            row.last_seen_address = j.value("last_seen_address", row.address);
            row.last_seen_at_ms = j.value("last_seen_at_ms", (int64_t)0);
            row.enrollment_state = j.value("enrollment_state", "");
            row.last_error = j.value("last_error", "");
            row.trusted = j.value("trusted", false);
            row.local = false;
            if (j.contains("address_history") && j["address_history"].is_array()) {
                for (const auto& v : j["address_history"]) {
                    if (v.is_string()) AddUniqueAddress(row.address_history, v.get<std::string>());
                }
            }
            AddUniqueAddress(row.address_history, row.address);
            if (row.address.empty()) continue;

            bool duplicate = false;
            for (auto& existing : m_devices) {
                if (SameDeviceIdentity(existing, row)) {
                    if (!existing.local) MergeDeviceRecord(existing, row);
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) m_devices.push_back(row);
        }
    } catch (...) {
        if (have_local) {
            m_devices.clear();
            m_devices.push_back(local);
        }
    }
}

void FleetPage::SaveDevices() const {
    if (m_devices_path.empty()) return;
    json root;
    root["schema"] = 2;
    root["truth_model"] = "explicit lab enrollment persists host-side trust; device_id/sensor_hash and mac_hash reconcile IP churn";
    root["devices"] = json::array();
    for (const auto& row : m_devices) {
        if (row.local) continue;
        root["devices"].push_back({
            {"name", row.name},
            {"role", row.role},
            {"address", row.address},
            {"hostname", row.hostname},
            {"os", row.os},
            {"state", row.state},
            {"device_id", row.device_id},
            {"sensor_hash", row.sensor_hash},
            {"mac_hash", row.mac_hash},
            {"last_seen_address", row.last_seen_address},
            {"last_seen_at_ms", row.last_seen_at_ms},
            {"address_history", row.address_history},
            {"enrollment_state", row.enrollment_state},
            {"last_error", row.last_error},
            {"trusted", row.trusted}
        });
    }
    std::ofstream f(m_devices_path);
    if (f.is_open()) f << root.dump(2);
}

} // namespace Client
