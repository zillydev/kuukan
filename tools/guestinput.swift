// guestinput — posts keyboard and mouse events inside the agent VM, where skhd's event tap and the bar see them.
// Built on the host by `make build` (the guest has no compiler) and copied in by scripts/agent-app.sh.
//
//   guestinput key alt+1          chord: modifiers (cmd ctrl alt shift fn) + one key
//   guestinput click X Y          left click at screen points
//   guestinput rclick X Y
//   guestinput move X Y
//   guestinput clear-notifications   closes every banner and alert stack; prints how many
import AppKit
import ApplicationServices
import CoreGraphics
import Foundation

let keyCodes: [String: CGKeyCode] = [
    "a": 0, "s": 1, "d": 2, "f": 3, "h": 4, "g": 5, "z": 6, "x": 7, "c": 8, "v": 9, "b": 11, "q": 12,
    "w": 13, "e": 14, "r": 15, "y": 16, "t": 17, "1": 18, "2": 19, "3": 20, "4": 21, "6": 22, "5": 23,
    "=": 24, "9": 25, "7": 26, "-": 27, "8": 28, "0": 29, "]": 30, "o": 31, "u": 32, "[": 33, "i": 34,
    "p": 35, "l": 37, "j": 38, "'": 39, "k": 40, ";": 41, "\\": 42, ",": 43, "/": 44, "n": 45, "m": 46,
    ".": 47, "`": 50, "return": 36, "tab": 48, "space": 49, "delete": 51, "escape": 53,
    "left": 123, "right": 124, "down": 125, "up": 126,
]

func fail(_ message: String) -> Never {
    FileHandle.standardError.write((message + "\n").data(using: .utf8)!)
    exit(2)
}

// The window server consumes posted events asynchronously: back-to-back events, or a process that exits
// right after posting, lose some of them. Each event gets time to land.
func post(_ event: CGEvent?) {
    guard let event else { fail("could not create event") }
    event.post(tap: .cghidEventTap)
    usleep(30_000)
}

let modifierKeys: [(CGEventFlags, CGKeyCode)] = [
    (.maskCommand, 55), (.maskShift, 56), (.maskAlternate, 58), (.maskControl, 59), (.maskSecondaryFn, 63),
]

func key(_ chord: String) {
    var flags: CGEventFlags = []
    var code: CGKeyCode?
    for part in chord.lowercased().split(separator: "+").map(String.init) {
        switch part {
        case "cmd", "command": flags.insert(.maskCommand)
        case "ctrl", "control": flags.insert(.maskControl)
        case "alt", "option": flags.insert(.maskAlternate)
        case "shift": flags.insert(.maskShift)
        case "fn": flags.insert(.maskSecondaryFn)
        default:
            guard let c = keyCodes[part] else { fail("unknown key \(part)") }
            code = c
        }
    }
    guard let code else { fail("no key in \(chord)") }
    // A real keyboard presses the modifiers as keys first; skhd tracks them from these events.
    let held = modifierKeys.filter { flags.contains($0.0) }
    var current: CGEventFlags = []
    for (flag, mod) in held {
        current.insert(flag)
        let event = CGEvent(keyboardEventSource: nil, virtualKey: mod, keyDown: true)
        event?.flags = current
        post(event)
    }
    for down in [true, false] {
        let event = CGEvent(keyboardEventSource: nil, virtualKey: code, keyDown: down)
        event?.flags = flags
        post(event)
    }
    for (flag, mod) in held.reversed() {
        current.remove(flag)
        let event = CGEvent(keyboardEventSource: nil, virtualKey: mod, keyDown: false)
        event?.flags = current
        post(event)
    }
}

func point(_ args: [String]) -> CGPoint {
    guard args.count >= 2, let x = Double(args[0]), let y = Double(args[1]) else { fail("need X Y") }
    return CGPoint(x: x, y: y)
}

func click(_ at: CGPoint, right: Bool) {
    let (down, up, button): (CGEventType, CGEventType, CGMouseButton) =
        right ? (.rightMouseDown, .rightMouseUp, .right) : (.leftMouseDown, .leftMouseUp, .left)
    post(CGEvent(mouseEventSource: nil, mouseType: .mouseMoved, mouseCursorPosition: at, mouseButton: button))
    post(CGEvent(mouseEventSource: nil, mouseType: down, mouseCursorPosition: at, mouseButton: button))
    post(CGEvent(mouseEventSource: nil, mouseType: up, mouseCursorPosition: at, mouseButton: button))
}

func axValue(_ element: AXUIElement, _ name: String) -> AnyObject? {
    var value: AnyObject?
    AXUIElementCopyAttributeValue(element, name as CFString, &value)
    return value
}

// Banners carry their close as an unnamed custom action ("Name:Clear All\nTarget:..."), not a button.
func clearNotifications(_ element: AXUIElement) -> Int {
    let subrole = axValue(element, kAXSubroleAttribute) as? String ?? ""
    if subrole.hasPrefix("AXNotificationCenterAlert") {
        var names: CFArray?
        AXUIElementCopyActionNames(element, &names)
        let close = ((names as? [String]) ?? []).first { $0.hasPrefix("Name:Clear All") || $0.hasPrefix("Name:Close") }
        if let close, AXUIElementPerformAction(element, close as CFString) == .success { return 1 }
    }
    return ((axValue(element, kAXChildrenAttribute) as? [AXUIElement]) ?? []).reduce(0) { $0 + clearNotifications($1) }
}

let args = Array(CommandLine.arguments.dropFirst())
switch args.first {
case "key":
    guard args.count > 1 else { fail("usage: guestinput key CHORD") }
    key(args[1])
case "click": click(point(Array(args.dropFirst())), right: false)
case "rclick": click(point(Array(args.dropFirst())), right: true)
case "move":
    post(CGEvent(mouseEventSource: nil, mouseType: .mouseMoved, mouseCursorPosition: point(Array(args.dropFirst())), mouseButton: .left))
case "clear-notifications":
    guard AXIsProcessTrusted() else { fail("guestinput has no Accessibility grant") }
    let center = NSRunningApplication.runningApplications(withBundleIdentifier: "com.apple.notificationcenterui").first
    let windows = center.map { axValue(AXUIElementCreateApplication($0.processIdentifier), kAXWindowsAttribute) as? [AXUIElement] ?? [] } ?? []
    print(windows.reduce(0) { $0 + clearNotifications($1) })
default: fail("usage: guestinput key CHORD | click X Y | rclick X Y | move X Y | clear-notifications")
}
