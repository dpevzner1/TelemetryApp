#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "window.h"
#include "diagnostic_log.h"

int WINAPI wWinMain(HINSTANCE hinstance, HINSTANCE, LPWSTR, int) {
    Client::DiagnosticLogInit();
    Client::InstallCrashDiagnostics();
    Client::DiagnosticLogInfo("wWinMain entered.");

    Client::AppWindow wnd;
    if (!wnd.Create(hinstance)) {
        Client::DiagnosticLogError("AppWindow::Create failed.");
        MessageBoxW(nullptr, L"Failed to create window or initialize Direct2D.",
                    L"Telemetry Client", MB_ICONERROR);
        Client::DiagnosticLogShutdown();
        return 1;
    }
    Client::DiagnosticLogInfo("AppWindow created; entering message loop.");
    wnd.RunMessageLoop();
    Client::DiagnosticLogInfo("Message loop exited.");
    Client::DiagnosticLogShutdown();
    return 0;
}
