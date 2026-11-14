import Foundation
import TonicCore

/// The bytes of one run on disk (the core does no file I/O: tc_rec_sink). The file is made by the first write, so a
/// run that opens no level leaves none; every write is flushed, as what a crash leaves behind is the point.
private final class RecordingFile {
    private let folder: URL
    private var file: UnsafeMutablePointer<FILE>?
    private var failed = false

    init(folder: URL) { self.folder = folder }
    deinit { if let file { fclose(file) } }

    func write(_ data: UnsafeRawPointer, _ size: Int) {
        if failed { return }
        if file == nil {
            try? FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
            let when = Recording.stamp()  // two runs in a second: never over the first
            var target = folder.appendingPathComponent("\(when)_metal.\(TC_REC_EXTENSION)"), n = 1
            while FileManager.default.fileExists(atPath: target.path), n < 1000 {
                n += 1
                target = folder.appendingPathComponent("\(when)_metal_\(n).\(TC_REC_EXTENSION)")
            }
            file = fopen(target.path, "wb")
            if file == nil {
                print("Not recording: can't write \(target.path)")
                failed = true
                return
            }
            print("Recording to \(target.path)")
        }
        if fwrite(data, 1, size, file) != size || fflush(file) != 0 {
            print("Recording stopped: the write failed")
            failed = true
        }
    }
}

/// The run being recorded (core: tc_rec.h; docs/RECORDING.md), as the SDL build's sdl/src/record.c: with Record runs on in
/// the Replays menu, a run, from a level opened from a menu through its portals back to a menu, leaves one .ttrec in the
/// recordings folder. F9 or the touchpad marks a moment.
@MainActor final class Recording {
    static let shared = Recording()
    private(set) var recorder: OpaquePointer?
    private(set) var marks: UInt32 = 0
    private var file: RecordingFile?

    /// TONIC_RECORDINGS: the folder instead of ~/Library/Application Support/OpenTonic/Recordings, or "off".
    static var folder: URL? {
        if let env = ProcessInfo.processInfo.environment["TONIC_RECORDINGS"], !env.isEmpty {
            return env.lowercased() == "off" ? nil : URL(fileURLWithPath: env)
        }
        return FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first?
            .appendingPathComponent("OpenTonic/Recordings")
    }

    /// Record runs, the Replays menu's toggle: off by default, as the files add up. Kept across launches (UserDefaults
    /// "recordRuns"); a new value counts from the next run.
    static var enabled: Bool {
        get { UserDefaults.standard.bool(forKey: "recordRuns") }
        set { UserDefaults.standard.set(newValue, forKey: "recordRuns") }
    }

    nonisolated static func stamp() -> String {
        let f = DateFormatter()
        f.locale = Locale(identifier: "en_US_POSIX")
        f.dateFormat = "yyyy-MM-dd_HHmmss"
        return f.string(from: Date())
    }

    /// The newest recording: the names start with their date.
    static func latest() -> URL? {
        guard let folder else { return nil }
        let files = (try? FileManager.default.contentsOfDirectory(at: folder, includingPropertiesForKeys: nil)) ?? []
        return files.filter { $0.pathExtension == TC_REC_EXTENSION }.max { $0.lastPathComponent < $1.lastPathComponent }
    }

    /// A new run, the last one closed.
    func start() {
        stop()
        guard Self.enabled, let folder = Self.folder else { return }
        let f = RecordingFile(folder: folder)
        recorder = tc_recorder_create({ user, data, size in
            guard let user, let data else { return }
            Unmanaged<RecordingFile>.fromOpaque(user).takeUnretainedValue().write(data, size)
        }, Unmanaged.passUnretained(f).toOpaque())
        file = f
        let info = ProcessInfo.processInfo
        note("frontend", "metal")
        note("build", Bundle.main.object(forInfoDictionaryKey: "OpenTonicBuild") as? String ?? "unknown")
        note("started", Self.stamp())
        note("os", info.operatingSystemVersionString)
        #if arch(arm64)
        note("cpu", "arm64")
        #elseif arch(x86_64)
        note("cpu", "x86_64")
        #endif
    }

    func note(_ key: String, _ value: String) {
        if let recorder { tc_recorder_note(recorder, key, value) }
    }

    /// "Look at this moment": kept with the frame and the state. False while no level is being recorded.
    @discardableResult func mark() -> Bool {
        guard let recorder else { return false }
        let n = tc_recorder_mark(recorder, nil)
        if n == 0 { return false }
        marks = n
        print("Mark \(n)")
        return true
    }

    /// Closes the run, with a level still open if there is one: going to a menu or quitting isn't a crash.
    func stop() {
        if let recorder { tc_recorder_free(recorder) }
        recorder = nil
        file = nil
        marks = 0
    }
}

/// Playing a recording back: the recorded frames at their own pace, level after level, the game's inputs locked. P pauses,
/// the camera turning round Ed. Esc, Start and its end go back to the Replays menu, Cmd-L to the level select.
@MainActor final class Playback {
    /// nil: an original demo, whose tape the viewer plays itself (tc_viewer_play_tape).
    private let raw: UnsafeMutablePointer<tc_replay>?
    private let bytes: UnsafeMutableRawBufferPointer?
    private var clock: Float = 0
    private(set) var segment: UInt32 = 0
    /// The frames wait and the camera turns round Ed (tc_replay_pause).
    private(set) var paused = false

    /// An original demo: watched as a recording is, nothing to step here.
    init() {
        raw = nil
        bytes = nil
    }

    init?(url: URL) {
        guard let data = try? Data(contentsOf: url) else { return nil }
        let replay = UnsafeMutablePointer<tc_replay>.allocate(capacity: 1)
        let buffer = UnsafeMutableRawBufferPointer.allocate(byteCount: max(data.count, 1), alignment: 8)
        data.copyBytes(to: buffer)
        guard tc_replay_open(replay, buffer.baseAddress, data.count) == TC_OK, replay.pointee.segment_count > 0 else {
            buffer.deallocate()
            replay.deallocate()
            return nil
        }
        raw = replay
        bytes = buffer
    }

    var segmentCount: UInt32 { raw?.pointee.segment_count ?? 0 }
    func info(_ i: UInt32) -> tc_replay_segment { raw!.pointee.segments[Int(i)] }

    func begin(_ i: UInt32, _ v: UnsafeMutablePointer<tc_viewer>) {
        segment = i
        if let raw { tc_replay_begin(raw, i, v, false) }
    }

    /// The frames real time has reached: a display frame may hold none or several of the recording's. False at the
    /// segment's end.
    func advance(_ dt: Float, _ v: UnsafeMutablePointer<tc_viewer>) -> Bool {
        guard let raw else {  // an original demo: false at the tape's end
            tc_viewer_update(v, dt)
            Sfx.shared.play(v)
            return !tc_viewer_tape_done(v)
        }
        if paused {
            tc_viewer_paused_look(v, dt)
            return true
        }
        clock += dt
        while clock > 0 {
            var step: Float = 0
            if !tc_replay_step(raw, &step) { return false }
            Sfx.shared.play(v)
            clock -= step
        }
        return true
    }

    func togglePause() {
        guard let raw else { return }  // a tape has no paused look
        paused.toggle()
        tc_replay_pause(raw, paused)
        clock = 0
    }

    func close() {
        guard let raw, let bytes else { return }
        tc_replay_close(raw)
        bytes.deallocate()
        raw.deallocate()
    }
}
