#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <eh.h>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>

#include "diagnostic_log.h"

namespace Client {
namespace {

std::mutex s_log_mutex;
std::ofstream s_log;

std::string ExeDir() {
    char exe_path[MAX_PATH]{};
    GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
    std::string dir(exe_path);
    auto pos = dir.find_last_of("\\/");
    return (pos == std::string::npos) ? "." : dir.substr(0, pos);
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

std::string Hex(DWORD v) {
    std::ostringstream os;
    os << "0x" << std::hex << std::uppercase << v;
    return os.str();
}

void WriteLine(const char* level, const std::string& msg) {
    std::lock_guard<std::mutex> lock(s_log_mutex);
    if (!s_log.is_open()) return;
    s_log << Timestamp()
          << " [" << level << "]"
          << " [pid=" << GetCurrentProcessId()
          << " tid=" << GetCurrentThreadId() << "] "
          << msg << "\n";
    s_log.flush();
}

LONG WINAPI UnhandledExceptionLogger(EXCEPTION_POINTERS* ep) {
    std::ostringstream os;
    os << "Unhandled exception";
    if (ep && ep->ExceptionRecord) {
        os << " code=" << Hex(ep->ExceptionRecord->ExceptionCode)
           << " address=" << ep->ExceptionRecord->ExceptionAddress
           << " flags=" << Hex(ep->ExceptionRecord->ExceptionFlags);
    }
    WriteLine("CRASH", os.str());
    WriteLine("CRASH", "Process will terminate after crash log flush.");
    return EXCEPTION_EXECUTE_HANDLER;
}

void TerminateLogger() {
    WriteLine("CRASH", "std::terminate invoked.");
    std::abort();
}

void SignalLogger(int sig) {
    std::ostringstream os;
    os << "C runtime signal received: " << sig;
    WriteLine("CRASH", os.str());
    std::_Exit(128 + sig);
}

} // namespace

void DiagnosticLogInit() {
    std::lock_guard<std::mutex> lock(s_log_mutex);
    const std::string path = ExeDir() + "\\telemetry_client_diagnostic.log";
    if (s_log.is_open()) s_log.close();
    s_log.open(path, std::ios::out | std::ios::trunc);
    if (!s_log.is_open()) return;

    s_log << Timestamp() << " [INFO] [pid=" << GetCurrentProcessId()
          << " tid=" << GetCurrentThreadId() << "] "
          << "TelemetryClient diagnostic log opened. This file is overwritten each run.\n";
    s_log << Timestamp() << " [INFO] [pid=" << GetCurrentProcessId()
          << " tid=" << GetCurrentThreadId() << "] Log path: " << path << "\n";
    char cwd[MAX_PATH]{};
    if (GetCurrentDirectoryA(MAX_PATH, cwd)) {
        s_log << Timestamp() << " [INFO] [pid=" << GetCurrentProcessId()
              << " tid=" << GetCurrentThreadId() << "] Working directory: " << cwd << "\n";
    }
    LPSTR cmd = GetCommandLineA();
    if (cmd) {
        s_log << Timestamp() << " [INFO] [pid=" << GetCurrentProcessId()
              << " tid=" << GetCurrentThreadId() << "] Command line: " << cmd << "\n";
    }
    s_log.flush();
}

void DiagnosticLogShutdown() {
    WriteLine("INFO", "TelemetryClient diagnostic log closing.");
    std::lock_guard<std::mutex> lock(s_log_mutex);
    if (s_log.is_open()) s_log.close();
}

void DiagnosticLogInfo(const std::string& msg) { WriteLine("INFO", msg); }
void DiagnosticLogWarn(const std::string& msg) { WriteLine("WARN", msg); }
void DiagnosticLogError(const std::string& msg) { WriteLine("ERROR", msg); }

void DiagnosticLogLastError(const std::string& context, DWORD error) {
    std::ostringstream os;
    os << context << " failed with GetLastError=" << error << " (" << Hex(error) << ")";
    WriteLine("ERROR", os.str());
}

void InstallCrashDiagnostics() {
    SetUnhandledExceptionFilter(UnhandledExceptionLogger);
    std::set_terminate(TerminateLogger);
    std::signal(SIGABRT, SignalLogger);
    std::signal(SIGFPE, SignalLogger);
    std::signal(SIGILL, SignalLogger);
    std::signal(SIGSEGV, SignalLogger);
    DiagnosticLogInfo("Crash diagnostics installed.");
}

} // namespace Client
