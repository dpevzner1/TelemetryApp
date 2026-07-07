#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include "service_control.h"
#include "diagnostic_log.h"

namespace Service {

static SERVICE_STATUS_HANDLE s_ssh   = nullptr;
static SERVICE_STATUS        s_ss    = {};

void SetStatusHandle(SERVICE_STATUS_HANDLE h) {
    s_ssh = h;
    s_ss.dwServiceType             = SERVICE_WIN32_OWN_PROCESS;
    s_ss.dwCurrentState            = SERVICE_START_PENDING;
    s_ss.dwControlsAccepted        = 0;
    s_ss.dwWin32ExitCode           = NO_ERROR;
    s_ss.dwServiceSpecificExitCode = 0;
    s_ss.dwCheckPoint              = 0;
    s_ss.dwWaitHint                = 3000;
}

void ReportStatus(DWORD state, DWORD exit_code, DWORD wait_hint) {
    static DWORD checkpoint = 1;
    s_ss.dwCurrentState  = state;
    s_ss.dwWin32ExitCode = exit_code;
    s_ss.dwWaitHint      = wait_hint;
    s_ss.dwControlsAccepted =
        (state == SERVICE_START_PENDING) ? 0
        : (SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_PAUSE_CONTINUE);
    s_ss.dwCheckPoint =
        (state == SERVICE_RUNNING || state == SERVICE_STOPPED) ? 0 : checkpoint++;
    if (s_ssh) SetServiceStatus(s_ssh, &s_ss);
}

void LogEvent(WORD type, const std::string& msg) {
    if (type == EVENTLOG_ERROR_TYPE)
        DiagnosticLogError(msg);
    else if (type == EVENTLOG_WARNING_TYPE)
        DiagnosticLogWarn(msg);
    else
        DiagnosticLogInfo(msg);

    HANDLE hEvt = RegisterEventSourceW(nullptr, SERVICE_NAME);
    if (!hEvt) return;
    std::wstring wmsg(msg.begin(), msg.end());
    const wchar_t* strs[1] = { wmsg.c_str() };
    ReportEventW(hEvt, type, 0, 0, nullptr, 1, 0, strs, nullptr);
    DeregisterEventSource(hEvt);
}

} // namespace Service
