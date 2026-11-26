// appearance — prints "Dark" or "Light" for the current *effective* macOS appearance.
//
// Why a compiled helper instead of the obvious one-liners:
//   • `osascript ... System Events get dark mode`  -> blocked: sketchybar lacks the
//     Apple Events (Automation) TCC grant, so it returns -1743 (and the grant goes
//     stale on every `brew upgrade sketchybar` anyway — ad-hoc signed, path/hash keyed).
//   • `defaults read -g AppleInterfaceStyle`        -> unset under macOS "Auto" appearance.
// NSApp.effectiveAppearance is correct under Auto AND needs no permission.
//
// Build:  swiftc appearance.swift -o appearance
import AppKit

let app = NSApplication.shared
app.setActivationPolicy(.accessory)    // window-server-connected but Dock-hidden
let isDark = app.effectiveAppearance
    .bestMatch(from: [.aqua, .darkAqua]) == .darkAqua
print(isDark ? "Dark" : "Light")
