#include "NativeWindowShift.h"

#include <juce_audio_utils/juce_audio_utils.h>

#if JUCE_WINDOWS

#include <windows.h>
#include <cmath>

namespace spa
{
namespace ui
{

int shiftNativeWindowX (void* nativeHandle, int deltaPx, double platformScale)
{
    if (nativeHandle == nullptr || deltaPx == 0)
        return 0;

    HWND root = GetAncestor ((HWND) nativeHandle, GA_ROOT);
    if (root == nullptr || ! IsWindowVisible (root))
        return 0;

    RECT before {};
    if (! GetWindowRect (root, &before))
        return 0;

    // GetWindowRect/SetWindowPos work in physical pixels; deltaPx is in
    // JUCE logical pixels, so convert before moving.
    const double scale = platformScale > 0.0 ? platformScale : 1.0;
    const LONG physicalDelta = (LONG) std::lround ((double) deltaPx * scale);
    if (physicalDelta == 0)
        return 0;

    LONG newX = before.left - physicalDelta;

    if (HMONITOR mon = MonitorFromWindow (root, MONITOR_DEFAULTTONEAREST))
    {
        MONITORINFO mi {};
        mi.cbSize = sizeof (MONITORINFO);
        if (GetMonitorInfo (mon, &mi))
        {
            const LONG w = before.right - before.left;
            if (newX < mi.rcWork.left)
                newX = mi.rcWork.left;
            if (newX + w > mi.rcWork.right)
                newX = mi.rcWork.right - w;
        }
    }

    SetWindowPos (root, nullptr, (int) newX, (int) before.top, 0, 0,
                  SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    RECT after {};
    if (! GetWindowRect (root, &after))
        return 0;

    const LONG appliedPhysical = before.left - after.left;
    if (appliedPhysical == 0)
        return 0;

    // Convert the verified physical move back to logical pixels so callers
    // always deal in the same units they passed in.
    return (int) std::lround ((double) appliedPhysical / scale);
}

} // namespace ui
} // namespace spa

#else // ! JUCE_WINDOWS

// Non-mac, non-Windows platforms (this project doesn't target any -- Linux
// build isn't shipped -- but keep the symbol defined so this fallback source
// file always provides it when NativeWindowShift.mm isn't compiled).
#if ! JUCE_MAC

namespace spa
{
namespace ui
{

int shiftNativeWindowX (void*, int, double)
{
    return 0;
}

} // namespace ui
} // namespace spa

#endif // ! JUCE_MAC
#endif // JUCE_WINDOWS
