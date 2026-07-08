#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <thread>
#include <atomic>
#include <string>
#include <cstdlib>
#include <conio.h>
#include "service_control.h"
#include "poll_loop.h"
#include "api_key_store.h"
#include "log_session.h"
#include "enterprise_config.h"
#include "fleet_heartbeat.h"
#include "diagnostic_log.h"
#include "ipc/pipe_server.h"
#include "ipc/http_server.h"

using namespace Service;

static std::atomic<bool>  g_stop{false};
static std::atomic<bool>* g_console_stop = nullptr;
static std::thread        g_poll_thread;
static std::thread        g_pipe_thread;
static std::thread        g_http_thread;
static std::thread        g_heartbeat_thread;

static void EnsureDir(const std::string& path) {
    DiagnosticLogInfo("Ensuring directory: " + path);
    std::string cur;
    for (char c : path) {
        cur += c;
        if (c == '\\' || c == '/') CreateDirectoryA(cur.c_str(), nullptr);
    }
    CreateDirectoryA(path.c_str(), nullptr);
}

static std::string ResolveDataDir() {
    std::string data_dir;
    char buf[512]{};
    size_t sz = 0;
    if (getenv_s(&sz, buf, sizeof(buf), "TELEMETRY_DATA_DIR") == 0 && sz > 1)
        data_dir = std::string(buf, sz - 1);
    if (data_dir.empty()) {
        char prog[MAX_PATH]{};
        if (GetEnvironmentVariableA("PROGRAMDATA", prog, MAX_PATH))
            data_dir = std::string(prog) + "\\TelemetryApp";
    }
    if (data_dir.empty()) data_dir = ".\\TelemetryAppData";
    DiagnosticLogInfo("Resolved data directory: " + data_dir);
    return data_dir;
}

static std::string ExeDir() {
    char exe_path[MAX_PATH]{};
    GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
    std::string exe_dir(exe_path);
    auto pos = exe_dir.find_last_of("\\/");
    if (pos != std::string::npos) exe_dir = exe_dir.substr(0, pos);
    return exe_dir;
}

static bool InitStores() {
    DiagnosticLogInfo("InitStores begin.");
    std::string data_dir = ResolveDataDir();
    EnsureDir(data_dir);
    EnsureDir(data_dir + "\\api_keys");
    EnsureDir(data_dir + "\\logs");
    EnsureDir(data_dir + "\\sensor");
    EnsureDir(data_dir + "\\remote");

    if (!EnterpriseConfigInit(data_dir)) {
        DiagnosticLogError("EnterpriseConfigInit failed.");
        return false;
    }

    std::string store_path = data_dir + "\\api_keys\\store.json";
    std::string api_md_path = ExeDir() + "\\API.md";
    DiagnosticLogInfo("API key store path: " + store_path);
    DiagnosticLogInfo("Generated API reference path: " + api_md_path);
    if (!KeyStoreInit(store_path, api_md_path)) {
        DiagnosticLogError("KeyStoreInit failed.");
        return false;
    }

    GetKeyStore().SetOnChange([](){
        GetKeyStore().GenerateApiMd("http://localhost:8765");
    });
    GetKeyStore().GenerateApiMd("http://localhost:8765");

    LogSessionStore::Instance().Init(data_dir);
    DiagnosticLogInfo("InitStores complete.");
    return true;
}

// -----------------------------------------------------------------------
// SCM control handler
// -----------------------------------------------------------------------
static DWORD WINAPI CtrlHandler(DWORD ctrl, DWORD, LPVOID, LPVOID) {
    switch (ctrl) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        ReportStatus(SERVICE_STOP_PENDING, NO_ERROR, 5000);
        g_stop.store(true, std::memory_order_release);
        return NO_ERROR;
    case SERVICE_CONTROL_PAUSE:
        ReportStatus(SERVICE_PAUSE_PENDING);
        // No-op for now — poll loop continues; future: rate-limit
        ReportStatus(SERVICE_PAUSED);
        return NO_ERROR;
    case SERVICE_CONTROL_CONTINUE:
        ReportStatus(SERVICE_CONTINUE_PENDING);
        ReportStatus(SERVICE_RUNNING);
        return NO_ERROR;
    case SERVICE_CONTROL_INTERROGATE:
        return NO_ERROR;
    default:
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

// -----------------------------------------------------------------------
// ServiceMain
// -----------------------------------------------------------------------
static void WINAPI ServiceMain(DWORD, LPWSTR*) {
    DiagnosticLogInfo("ServiceMain entered.");
    SERVICE_STATUS_HANDLE ssh = RegisterServiceCtrlHandlerExW(
        SERVICE_NAME, CtrlHandler, nullptr);
    if (!ssh) {
        DiagnosticLogLastError("RegisterServiceCtrlHandlerExW");
        return;
    }

    SetStatusHandle(ssh);
    ReportStatus(SERVICE_START_PENDING, NO_ERROR, 10000);

    // Install recovery actions so SCM auto-restarts us on crash
    SERVICE_FAILURE_ACTIONSW fa{};
    fa.dwResetPeriod = 86400;  // reset failure count after 1 day
    fa.cActions = 3;
    SC_ACTION actions[3] = {
        {SC_ACTION_RESTART, 5000},
        {SC_ACTION_RESTART, 10000},
        {SC_ACTION_RESTART, 30000},
    };
    fa.lpsaActions = actions;
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm) {
        SC_HANDLE svc = OpenServiceW(scm, SERVICE_NAME, SERVICE_CHANGE_CONFIG);
        if (svc) { ChangeServiceConfig2W(svc, SERVICE_CONFIG_FAILURE_ACTIONS, &fa); CloseServiceHandle(svc); }
        CloseServiceHandle(scm);
    }

    if (!InitStores()) {
        ReportStatus(SERVICE_STOPPED, ERROR_FUNCTION_FAILED);
        return;
    }

    // Init subsystems
    DiagnosticLogInfo("Initializing poll loop.");
    if (!PollLoopInit()) {
        ReportStatus(SERVICE_STOPPED, ERROR_FUNCTION_FAILED);
        return;
    }
    DiagnosticLogInfo("Initializing named pipe server.");
    if (!PipeServerInit()) {
        LogEvent(EVENTLOG_WARNING_TYPE, "Named pipe server init failed — IPC push unavailable");
    }
    DiagnosticLogInfo("Initializing HTTP server.");
    if (!HttpServerInit()) {
        LogEvent(EVENTLOG_WARNING_TYPE, "HTTP server init failed — REST API unavailable");
    }

    ReportStatus(SERVICE_RUNNING);
    LogEvent(EVENTLOG_INFORMATION_TYPE, "TelemetryService started");

    // Spawn worker threads
    DiagnosticLogInfo("Starting service worker threads.");
    g_poll_thread = std::thread([&]{ PollLoopRun(g_stop); });
    g_pipe_thread = std::thread([&]{ PipeServerRun(g_stop); });
    g_http_thread = std::thread([&]{ HttpServerRun(g_stop); });
    g_heartbeat_thread = std::thread([&]{ FleetHeartbeatRun(g_stop); });

    g_poll_thread.join();
    g_pipe_thread.join();
    g_http_thread.join();
    g_heartbeat_thread.join();

    PollLoopShutdown();
    PipeServerShutdown();
    HttpServerShutdown();

    LogEvent(EVENTLOG_INFORMATION_TYPE, "TelemetryService stopped");
    ReportStatus(SERVICE_STOPPED);
    DiagnosticLogShutdown();
}

// -----------------------------------------------------------------------
// Entry point — handles both SCM launch and --install/--uninstall/--console
// -----------------------------------------------------------------------
int wmain(int argc, wchar_t* argv[]) {
    DiagnosticLogInit();
    InstallCrashDiagnostics();
    DiagnosticLogInfo("wmain entered.");

    // Command-line install/uninstall/console helpers
    if (argc > 1) {
        if (wcscmp(argv[1], L"--install") == 0 ||
            wcscmp(argv[1], L"--install-auto") == 0 ||
            wcscmp(argv[1], L"--install-manual") == 0) {
            DiagnosticLogInfo("Install command received.");
            DWORD start_type = SERVICE_AUTO_START;
            if (wcscmp(argv[1], L"--install-manual") == 0) start_type = SERVICE_DEMAND_START;
            if (argc > 2 && (wcscmp(argv[2], L"--manual") == 0 || wcscmp(argv[2], L"manual") == 0))
                start_type = SERVICE_DEMAND_START;
            if (argc > 2 && (wcscmp(argv[2], L"--auto") == 0 || wcscmp(argv[2], L"auto") == 0))
                start_type = SERVICE_AUTO_START;
            wchar_t path[MAX_PATH];
            GetModuleFileNameW(nullptr, path, MAX_PATH);
            SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
            if (!scm) {
                DiagnosticLogLastError("OpenSCManagerW install");
                DiagnosticLogShutdown();
                return GetLastError();
            }
            SC_HANDLE svc = CreateServiceW(scm, SERVICE_NAME, SERVICE_DISPLAY,
                SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS,
                start_type, SERVICE_ERROR_NORMAL,
                path, nullptr, nullptr, nullptr, nullptr, nullptr);
            if (svc) {
                CloseServiceHandle(svc);
                wprintf(start_type == SERVICE_AUTO_START
                    ? L"Service installed with automatic startup.\n"
                    : L"Service installed with manual startup.\n");
            }
            else {
                DWORD err = GetLastError();
                if (err == ERROR_SERVICE_EXISTS) {
                    SC_HANDLE existing = OpenServiceW(scm, SERVICE_NAME, SERVICE_CHANGE_CONFIG);
                    if (existing) {
                        BOOL changed = ChangeServiceConfigW(existing,
                            SERVICE_NO_CHANGE, start_type, SERVICE_NO_CHANGE,
                            nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
                        CloseServiceHandle(existing);
                        if (changed) {
                            wprintf(start_type == SERVICE_AUTO_START
                                ? L"Existing service changed to automatic startup.\n"
                                : L"Existing service changed to manual startup.\n");
                        } else {
                            DiagnosticLogLastError("ChangeServiceConfigW");
                            wprintf(L"Service exists, but startup change failed: %lu\n", GetLastError());
                        }
                    } else {
                        DiagnosticLogLastError("OpenServiceW existing");
                        wprintf(L"Service exists, but could not open it: %lu\n", GetLastError());
                    }
                } else {
                    DiagnosticLogLastError("CreateServiceW");
                    wprintf(L"Install failed: %lu\n", err);
                }
            }
            CloseServiceHandle(scm);
            DiagnosticLogShutdown();
            return 0;
        }
        if (wcscmp(argv[1], L"--uninstall") == 0) {
            DiagnosticLogInfo("Uninstall command received.");
            SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
            if (!scm) {
                DiagnosticLogLastError("OpenSCManagerW uninstall");
                DiagnosticLogShutdown();
                return GetLastError();
            }
            SC_HANDLE svc = OpenServiceW(scm, SERVICE_NAME, DELETE);
            if (svc) {
                DeleteService(svc); CloseServiceHandle(svc);
                wprintf(L"Service uninstalled.\n");
            }
            CloseServiceHandle(scm);
            DiagnosticLogShutdown();
            return 0;
        }
        if (wcscmp(argv[1], L"--console") == 0 || wcscmp(argv[1], L"console") == 0 ||
            wcscmp(argv[1], L"--console-noninteractive") == 0 || wcscmp(argv[1], L"--run") == 0 ||
            wcscmp(argv[1], L"run") == 0) {
            // Run directly in console for debugging
            const bool noninteractive =
                (wcscmp(argv[1], L"--console-noninteractive") == 0 || wcscmp(argv[1], L"--run") == 0 ||
                 wcscmp(argv[1], L"run") == 0);
            DiagnosticLogInfo(noninteractive ? "Run command received." : "Console command received.");
            std::atomic<bool> stop{false};
            g_console_stop = &stop;
            SetConsoleCtrlHandler([](DWORD t) -> BOOL {
                if (t == CTRL_C_EVENT || t == CTRL_BREAK_EVENT || t == CTRL_CLOSE_EVENT) {
                    if (g_console_stop) g_console_stop->store(true);
                    return TRUE;
                }
                return FALSE;
            }, TRUE);
            if (!InitStores()) {
                fwprintf(stderr, L"InitStores failed.\n");
                DiagnosticLogError("Console startup stopped: InitStores failed.");
                g_console_stop = nullptr;
                DiagnosticLogShutdown();
                return 1;
            }
            DiagnosticLogInfo("Console initializing poll loop.");
            if (!PollLoopInit()) {
                fwprintf(stderr, L"PollLoopInit failed.\n");
                DiagnosticLogError("Console startup stopped: PollLoopInit failed.");
                g_console_stop = nullptr;
                DiagnosticLogShutdown();
                return 1;
            }
            DiagnosticLogInfo("Console initializing named pipe server.");
            if (!PipeServerInit()) {
                fwprintf(stderr, L"PipeServerInit failed; continuing without named-pipe IPC.\n");
            }
            DiagnosticLogInfo("Console initializing HTTP server.");
            if (!HttpServerInit()) {
                fwprintf(stderr, L"HttpServerInit failed; continuing without HTTP API.\n");
            }
            DiagnosticLogInfo("Console starting worker threads.");
            g_poll_thread = std::thread([&]{ PollLoopRun(stop); });
            g_pipe_thread = std::thread([&]{ PipeServerRun(stop); });
            g_http_thread = std::thread([&]{ HttpServerRun(stop); });
            g_heartbeat_thread = std::thread([&]{ FleetHeartbeatRun(stop); });
            wprintf(noninteractive
                ? L"Running in non-interactive console mode. Stop the process to exit.\n"
                : L"Running in console. Press Enter to stop.\n");
            if (noninteractive) {
                while (!stop.load()) Sleep(200);
            } else {
                _getwch();
            }
            stop.store(true);
            g_poll_thread.join(); g_pipe_thread.join(); g_http_thread.join(); g_heartbeat_thread.join();
            PollLoopShutdown(); PipeServerShutdown(); HttpServerShutdown();
            g_console_stop = nullptr;
            DiagnosticLogInfo("Console run stopped cleanly.");
            DiagnosticLogShutdown();
            return 0;
        }
    }

    // Normal SCM dispatch
    DiagnosticLogInfo("Starting SCM dispatcher.");
    SERVICE_TABLE_ENTRYW table[] = {
        { const_cast<wchar_t*>(SERVICE_NAME), ServiceMain },
        { nullptr, nullptr }
    };
    StartServiceCtrlDispatcherW(table);
    DiagnosticLogLastError("StartServiceCtrlDispatcherW");
    DiagnosticLogShutdown();
    return 0;
}
