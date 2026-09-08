#pragma once

namespace spa
{
namespace ui
{

// Best-effort move of the OS-level top-level window containing the given
// native view/handle: shifts it left by deltaPx logical pixels (or right, if
// deltaPx is negative), clamped to stay within its display's usable area.
// nativeHandle is juce::ComponentPeer::getNativeHandle() -- an NSView* on
// macOS, an HWND on Windows.
//
// Returns the actual x delta, in JUCE logical pixels, that was VERIFIED to
// take effect (reads the window's frame before and after the move and
// compares): positive means the window moved LEFT by that many logical
// pixels, negative means it moved RIGHT, and 0 means either there's no
// live/visible top-level window to move (e.g. an out-of-process host whose
// peer is a remote window proxy -- Logic's AUHostingService on macOS is the
// known case: the call "succeeds" but the proxy just no-ops it) or the move
// had no effect (e.g. clamped at the screen edge with the window already
// flush against it). Callers should do nothing further when this returns 0:
// the accepted fallback is that the synth simply grows to the right in that
// host, same as before this feature existed.
//
// The returned value can be LESS than the requested deltaPx if clamping to
// the display's usable area reduced the move. Callers that later want to
// undo the shift (e.g. on drawer close) must pass back exactly
// -<the value this function returned on the way out>, NOT -deltaPx -- doing
// the latter would creep the window one clamp's worth to the right on every
// open/close cycle whenever the open move got clamped.
//
// platformScale is juce::ComponentPeer::getPlatformScaleFactor() -- the
// ratio between JUCE logical pixels and the OS's physical/device pixels.
// Needed on Windows, where GetWindowRect/SetWindowPos operate in physical
// pixels: deltaPx is converted to physical pixels (rounded) before moving,
// and the verified applied delta is converted back to logical pixels for
// the return value, so callers always deal in logical pixels regardless of
// platform. Unused on macOS: NSWindow frames are already reported in
// points, which equal JUCE logical pixels 1:1 regardless of the display's
// backing scale (Retina or not).
//
// No-op (always returns 0) on any platform other than macOS/Windows, and
// whenever there's no live/visible top-level window to move.
int shiftNativeWindowX (void* nativeHandle, int deltaPx, double platformScale);

} // namespace ui
} // namespace spa
