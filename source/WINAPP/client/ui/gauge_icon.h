#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace Client {

// Create a gauge/speedometer HICON at the given pixel size.
// The caller owns the HICON and must DestroyIcon() when done.
HICON CreateGaugeIcon(int size);

// Convenience: create both ICON_BIG (32px) and ICON_SMALL (16px)
// and set them on the window.
void SetGaugeIcon(HWND hwnd);

} // namespace Client
