// bt_battery — prints connected Bluetooth devices' battery as JSON: [{"name": ..., "battery": N}].
// Compiled with an embedded Info.plist carrying NSBluetoothAlwaysUsageDescription.
import Foundation
import IOBluetooth

// TCC checks the responsible process's Info.plist, so re-exec disclaimed to own Bluetooth access.
@_silgen_name("responsibility_spawnattrs_setdisclaim")
func responsibility_spawnattrs_setdisclaim(_ attr: UnsafeMutablePointer<posix_spawnattr_t?>, _ disclaim: Int32) -> Int32

if getenv("BT_BATTERY_DISCLAIMED") == nil {
    setenv("BT_BATTERY_DISCLAIMED", "1", 1)
    var attr: posix_spawnattr_t?
    posix_spawnattr_init(&attr)
    _ = responsibility_spawnattrs_setdisclaim(&attr, 1)
    guard let path = Bundle.main.executablePath else { exit(1) }
    var argv: [UnsafeMutablePointer<CChar>?] = [strdup(path), nil]
    var pid: pid_t = 0
    guard posix_spawn(&pid, path, nil, &attr, &argv, environ) == 0 else { exit(1) }
    var status: Int32 = 0
    waitpid(pid, &status, 0)
    exit((status >> 8) & 0xff)
}

func percent(_ d: IOBluetoothDevice, _ key: String) -> Int {
    guard d.responds(to: NSSelectorFromString(key)) else { return 0 }
    return (d.value(forKey: key) as? NSNumber)?.intValue ?? 0
}

// Sony headphones' control channel (Gadgetbridge "V2" protocol) for HFP-less macOS 27 reads.
let sonyV2UUID: [UInt8] = [0x95, 0x6C, 0x7B, 0x26, 0xD4, 0x9A, 0x4B, 0xA8, 0xB0, 0x3F, 0xB1, 0x7D, 0x39, 0x3C, 0xB6, 0xE2]

func sonyChannel(_ d: IOBluetoothDevice) -> BluetoothRFCOMMChannelID? {
    for case let r as IOBluetoothSDPServiceRecord in d.services ?? [] {
        guard let el = r.getAttributeDataElement(1) else { continue }
        let elems = el.getArrayValue()?.compactMap { $0 as? IOBluetoothSDPDataElement } ?? [el]
        guard elems.contains(where: { $0.getUUIDValue().map { [UInt8]($0 as NSData as Data) } == sonyV2UUID }) else { continue }
        var ch: BluetoothRFCOMMChannelID = 0
        if r.getRFCOMMChannelID(&ch) == kIOReturnSuccess { return ch }
    }
    return nil
}

func sonyFrame(_ type: UInt8, _ seq: UInt8, _ payload: [UInt8]) -> [UInt8] {
    let n = UInt32(payload.count)
    var body: [UInt8] = [type, seq, UInt8(n >> 24), UInt8(n >> 16 & 0xff), UInt8(n >> 8 & 0xff), UInt8(n & 0xff)] + payload
    body.append(UInt8(body.reduce(0) { $0 + Int($1) } & 0xff))
    var out: [UInt8] = [0x3e]
    for b in body {
        if b == 0x3c || b == 0x3d || b == 0x3e { out += [0x3d, b & 0xef] } else { out.append(b) }
    }
    return out + [0x3c]
}

// Init handshake, then BATTERY_LEVEL_REQUEST (22 00); reply is 23 00 <level> <charging>.
final class SonyBatteryQuery: NSObject, IOBluetoothRFCOMMChannelDelegate {
    var level: Int?
    var opened = false
    var finished = false
    var closed = false
    private var buffer: [UInt8] = []
    private var ackSeq: UInt8 = 0
    private var requested = false

    private func send(_ ch: IOBluetoothRFCOMMChannel, _ frame: [UInt8]) {
        var f = frame
        ch.writeSync(&f, length: UInt16(f.count))
    }

    private func sendInit(_ ch: IOBluetoothRFCOMMChannel) {
        guard !requested && !finished && !closed else { return }
        send(ch, sonyFrame(0x0c, 0, [0x00, 0x00]))
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.25) { [weak self, weak ch] in
            guard let self, let ch else { return }
            self.sendInit(ch)
        }
    }

    func rfcommChannelOpenComplete(_ ch: IOBluetoothRFCOMMChannel!, status: IOReturn) {
        opened = true
        guard status == kIOReturnSuccess else { finished = true; return }
        sendInit(ch)
    }

    func rfcommChannelData(_ ch: IOBluetoothRFCOMMChannel!, data ptr: UnsafeMutableRawPointer!, length n: Int) {
        buffer += UnsafeBufferPointer(start: ptr.assumingMemoryBound(to: UInt8.self), count: n)
        while let start = buffer.firstIndex(of: 0x3e), let end = buffer[start...].firstIndex(of: 0x3c) {
            var msg: [UInt8] = []
            var valid = true
            var i = start + 1
            while i < end {
                if buffer[i] == 0x3d {
                    guard i + 1 < end else { valid = false; break }
                    i += 1
                    msg.append(buffer[i] | 0x10)
                } else {
                    msg.append(buffer[i])
                }
                i += 1
            }
            buffer.removeSubrange(...end)
            guard valid, msg.count >= 7 else { continue }
            let expectedChecksum = UInt8(msg.dropLast().reduce(0) { $0 + Int($1) } & 0xff)
            guard msg.last == expectedChecksum else { continue }
            let type = msg[0], seq = msg[1], payload = Array(msg[6..<(msg.count - 1)])
            if type == 0x01 { ackSeq = seq; continue }
            send(ch, sonyFrame(0x01, 1 &- seq, []))
            if payload.first == 0x01 && !requested {
                requested = true
                send(ch, sonyFrame(0x0c, ackSeq, [0x22, 0x00]))
            } else if payload.first == 0x23 && payload.count >= 3 {
                level = Int(payload[2])
                finished = true
            }
        }
    }

    func rfcommChannelClosed(_ ch: IOBluetoothRFCOMMChannel!) { finished = true; closed = true }
}

func runLoop(until done: () -> Bool, timeout: TimeInterval) {
    let deadline = Date().addingTimeInterval(timeout)
    while !done() && Date() < deadline {
        RunLoop.current.run(until: Date().addingTimeInterval(0.05))
    }
}

func sonyBattery(_ d: IOBluetoothDevice) -> Int {
    guard let chID = sonyChannel(d) else { return 0 }
    for _ in 0..<2 {
        let query = SonyBatteryQuery()
        var ch: IOBluetoothRFCOMMChannel?
        guard d.openRFCOMMChannelAsync(&ch, withChannelID: chID, delegate: query) == kIOReturnSuccess else { return 0 }
        runLoop(until: { query.opened }, timeout: 1.5)
        if query.opened { runLoop(until: { query.finished }, timeout: 3) }
        ch?.close()
        runLoop(until: { query.closed }, timeout: 1)
        if let level = query.level { return level }
    }
    return 0
}

var result: [[String: Any]] = []
for case let d as IOBluetoothDevice in IOBluetoothDevice.pairedDevices() ?? [] where d.isConnected() {
    let batt = percent(d, "batteryPercentSingle")
    var level = batt > 0 ? batt : percent(d, "batteryPercentCombined")
    if level == 0 { level = sonyBattery(d) }
    if level > 0 {
        result.append(["name": d.name ?? "", "battery": level])
    }
}

if let data = try? JSONSerialization.data(withJSONObject: result),
   let text = String(data: data, encoding: .utf8) {
    print(text)
} else {
    print("[]")
}
