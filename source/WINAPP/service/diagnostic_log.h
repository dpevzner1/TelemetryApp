#pragma once

#include <string>
#include <windows.h>

namespace Service {

void DiagnosticLogInit();
void DiagnosticLogShutdown();
void DiagnosticLogInfo(const std::string& msg);
void DiagnosticLogWarn(const std::string& msg);
void DiagnosticLogError(const std::string& msg);
void DiagnosticLogLastError(const std::string& context, DWORD error = GetLastError());
void InstallCrashDiagnostics();

} // namespace Service
