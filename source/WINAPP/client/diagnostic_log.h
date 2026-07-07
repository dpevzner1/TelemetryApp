#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

namespace Client {

void DiagnosticLogInit();
void DiagnosticLogShutdown();
void DiagnosticLogInfo(const std::string& msg);
void DiagnosticLogWarn(const std::string& msg);
void DiagnosticLogError(const std::string& msg);
void DiagnosticLogLastError(const std::string& context, DWORD error = GetLastError());
void InstallCrashDiagnostics();

} // namespace Client
