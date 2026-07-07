#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

namespace Service {

// SCM registration and Windows Event Log helpers
bool RegisterAndStart();
void ReportStatus(DWORD state, DWORD exit_code = NO_ERROR, DWORD wait_hint = 0);
void LogEvent(WORD type, const std::string& msg);  // EVENTLOG_INFORMATION_TYPE etc.

// Called by ServiceMain on entry
void SetStatusHandle(SERVICE_STATUS_HANDLE h);

// Service name as seen by SCM
constexpr wchar_t SERVICE_NAME[] = L"TelemetryService";
constexpr wchar_t SERVICE_DISPLAY[] = L"Telemetry Service";

} // namespace Service
