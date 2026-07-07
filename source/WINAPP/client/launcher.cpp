#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace {

std::ofstream g_log;

std::wstring ExeDirW() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir(path);
    auto pos = dir.find_last_of(L"\\/");
    return (pos == std::wstring::npos) ? L"." : dir.substr(0, pos);
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, &out[0], needed, nullptr, nullptr);
    return out;
}

std::string Timestamp() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    std::ostringstream os;
    os << std::setfill('0')
       << std::setw(4) << st.wYear << "-"
       << std::setw(2) << st.wMonth << "-"
       << std::setw(2) << st.wDay << " "
       << std::setw(2) << st.wHour << ":"
       << std::setw(2) << st.wMinute << ":"
       << std::setw(2) << st.wSecond << "."
       << std::setw(3) << st.wMilliseconds;
    return os.str();
}

void Log(const std::string& level, const std::string& msg) {
    if (!g_log.is_open()) return;
    g_log << Timestamp() << " [" << level << "] [pid=" << GetCurrentProcessId()
          << " tid=" << GetCurrentThreadId() << "] " << msg << "\n";
    g_log.flush();
}

void InitLog(const std::wstring& dir) {
    std::wstring path = dir + L"\\TelemetryApp_launcher.log";
    g_log.open(WideToUtf8(path), std::ios::out | std::ios::trunc);
    Log("INFO", "TelemetryApp launcher log opened. This file is overwritten each run.");
    Log("INFO", "Executable directory: " + WideToUtf8(dir));
}

bool IsProcessRunning(const wchar_t* exe_name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exe_name) == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

bool StartProcess(const std::wstring& exe, const std::wstring& args, WORD show, PROCESS_INFORMATION& pi) {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = show;

    std::wstring cmd = L"\"" + exe + L"\"";
    if (!args.empty()) cmd += L" " + args;

    std::wstring dir = ExeDirW();
    BOOL ok = CreateProcessW(
        exe.c_str(),
        &cmd[0],
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        dir.c_str(),
        &si,
        &pi);
    if (!ok) {
        Log("ERROR", "CreateProcess failed for " + WideToUtf8(exe) +
            " GetLastError=" + std::to_string(GetLastError()));
        return false;
    }

    Log("INFO", "Started " + WideToUtf8(exe) + " pid=" + std::to_string(pi.dwProcessId));
    return true;
}

bool TryStartInstalledService() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) {
        Log("WARN", "OpenSCManager failed GetLastError=" + std::to_string(GetLastError()));
        return false;
    }

    SC_HANDLE svc = OpenServiceW(scm, L"TelemetryService", SERVICE_QUERY_STATUS | SERVICE_START);
    if (!svc) {
        Log("INFO", "TelemetryService is not installed or not accessible. GetLastError=" +
            std::to_string(GetLastError()));
        CloseServiceHandle(scm);
        return false;
    }

    SERVICE_STATUS_PROCESS ssp{};
    DWORD needed = 0;
    if (QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO,
                             reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &needed)) {
        if (ssp.dwCurrentState == SERVICE_RUNNING) {
            Log("INFO", "TelemetryService is already running.");
            CloseServiceHandle(svc);
            CloseServiceHandle(scm);
            return true;
        }
        if (ssp.dwCurrentState == SERVICE_START_PENDING) {
            Log("INFO", "TelemetryService is already start-pending.");
            Sleep(2500);
            CloseServiceHandle(svc);
            CloseServiceHandle(scm);
            return true;
        }
    }

    if (!StartServiceW(svc, 0, nullptr)) {
        DWORD err = GetLastError();
        if (err != ERROR_SERVICE_ALREADY_RUNNING) {
            Log("WARN", "StartService failed GetLastError=" + std::to_string(err));
            CloseServiceHandle(svc);
            CloseServiceHandle(scm);
            return false;
        }
    }

    Log("INFO", "StartService requested for TelemetryService.");
    Sleep(2500);
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return true;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    std::wstring dir = ExeDirW();
    InitLog(dir);

    std::wstring service_exe = dir + L"\\telemetry_service.exe";
    std::wstring client_exe = dir + L"\\telemetry_client.exe";

    DWORD service_attr = GetFileAttributesW(service_exe.c_str());
    DWORD client_attr = GetFileAttributesW(client_exe.c_str());
    if (service_attr == INVALID_FILE_ATTRIBUTES || client_attr == INVALID_FILE_ATTRIBUTES) {
        Log("ERROR", "Required executables are missing from the launcher folder.");
        MessageBoxW(nullptr,
            L"TelemetryApp requires telemetry_service.exe and telemetry_client.exe in the same folder.",
            L"TelemetryApp", MB_ICONERROR);
        return 1;
    }

    bool started_service = false;
    PROCESS_INFORMATION service_pi{};
    if (IsProcessRunning(L"telemetry_service.exe")) {
        Log("INFO", "telemetry_service.exe is already running; launcher will not start or stop another service.");
    } else {
        bool scm_started = TryStartInstalledService();
        if (scm_started) {
            Log("INFO", "Launcher used installed TelemetryService.");
        } else if (!StartProcess(service_exe, L"--run", SW_HIDE, service_pi)) {
            MessageBoxW(nullptr, L"Failed to start telemetry_service.exe. Check TelemetryApp_launcher.log.",
                L"TelemetryApp", MB_ICONERROR);
            return 2;
        } else {
            started_service = true;
            Sleep(2000);
        }
    }

    PROCESS_INFORMATION client_pi{};
    if (!StartProcess(client_exe, L"", SW_SHOWNORMAL, client_pi)) {
        if (started_service) {
            TerminateProcess(service_pi.hProcess, 1);
            CloseHandle(service_pi.hProcess);
            CloseHandle(service_pi.hThread);
        }
        MessageBoxW(nullptr, L"Failed to start telemetry_client.exe. Check TelemetryApp_launcher.log.",
            L"TelemetryApp", MB_ICONERROR);
        return 3;
    }

    Log("INFO", "Waiting for telemetry_client.exe to exit.");
    WaitForSingleObject(client_pi.hProcess, INFINITE);

    DWORD client_exit = 0;
    GetExitCodeProcess(client_pi.hProcess, &client_exit);
    Log("INFO", "telemetry_client.exe exited with code " + std::to_string(client_exit));
    CloseHandle(client_pi.hProcess);
    CloseHandle(client_pi.hThread);

    if (started_service) {
        Log("INFO", "Stopping telemetry_service.exe started by launcher.");
        TerminateProcess(service_pi.hProcess, 0);
        WaitForSingleObject(service_pi.hProcess, 5000);
        CloseHandle(service_pi.hProcess);
        CloseHandle(service_pi.hThread);
    }

    Log("INFO", "TelemetryApp launcher finished.");
    if (g_log.is_open()) g_log.close();
    return static_cast<int>(client_exit);
}
