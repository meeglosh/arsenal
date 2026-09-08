#include "NativeWindowShift.h"

#include <juce_audio_utils/juce_audio_utils.h>

#if JUCE_MAC

#import <Cocoa/Cocoa.h>
#include <cmath>

namespace spa
{
namespace ui
{

int shiftNativeWindowX (void* nativeHandle, int deltaPx, double /*platformScale*/)
{
    // platformScale is unused here: NSWindow frames are reported in points,
    // which already equal JUCE logical pixels 1:1 regardless of the
    // display's backing (Retina) scale -- only Windows' physical-pixel APIs
    // need the conversion (see NativeWindowShift.cpp).
    if (nativeHandle == nullptr || deltaPx == 0)
        return 0;

    NSView* view = (NSView*) nativeHandle;
    NSWindow* window = [view window];

    // An out-of-process host (Logic's AUHostingService is the known case)
    // hands us a real-looking NSView whose -window is either nil or a remote
    // proxy that silently no-ops setFrameOrigin: -- the before/after compare
    // below is what actually distinguishes that from an in-process window we
    // can move (the standalone, or a host that really does host us in-process).
    if (window == nil || ! [window isVisible])
        return 0;

    const NSRect before = [window frame];

    NSRect moved = before;
    moved.origin.x -= (CGFloat) deltaPx;

    // Clamp within the window's current screen's visible area (below the
    // menu bar, above the dock) rather than let it run off the display.
    if (NSScreen* screen = [window screen])
    {
        const NSRect visible = [screen visibleFrame];
        if (moved.origin.x < visible.origin.x)
            moved.origin.x = visible.origin.x;
        if (moved.origin.x + moved.size.width > visible.origin.x + visible.size.width)
            moved.origin.x = visible.origin.x + visible.size.width - moved.size.width;
    }

    [window setFrameOrigin: moved.origin];

    const NSRect after = [window frame];
    return (int) std::lround ((double) before.origin.x - (double) after.origin.x);
}

} // namespace ui
} // namespace spa

#endif // JUCE_MAC
