import AVFoundation
import os

/// The voices, mixed on the audio thread: the running engine pulls them, so starting a sound is only a write to this
/// table. Nothing here may wait on the audio system: AVAudioPlayer's play() blocks its caller for 16 ms or more, which
/// from inside the renderer's draw is a dropped frame for every footstep. (AVAudioPlayerNode's play() and stop() take
/// 11 ms.) As the SDL build's sfx.c, whose voices are audio streams bound to the open device.
private final class Mixer: @unchecked Sendable {
    private struct Voice {
        var samples: UnsafePointer<Float>?  // nil: free. Mono, at Sfx.sampleRate; owned by Sfx
        var count = 0
        var at = 0
        var volume: Float = 0
        var loop = -1  // the loop event it plays, -1 = a one-shot sound
    }

    private static let capacity = 32  // a long run peaked at 7
    private let voices = UnsafeMutablePointer<Voice>.allocate(capacity: capacity)
    private let lock = os_unfair_lock_t.allocate(capacity: 1)
    private var isMuted = false

    init() {
        voices.initialize(repeating: Voice(), count: Self.capacity)
        lock.initialize(to: os_unfair_lock())
    }

    private func locked<T>(_ body: () -> T) -> T {
        os_unfair_lock_lock(lock)
        defer { os_unfair_lock_unlock(lock) }
        return body()
    }

    var muted: Bool {
        get { locked { isMuted } }
        set { locked { isMuted = newValue } }
    }

    /// A free voice plays `sound`; all busy, it isn't played. loop: its event, whose voice goes on if it already plays.
    func start(_ sound: UnsafeBufferPointer<Float>, volume: Float, loop: Int?) {
        guard let samples = sound.baseAddress, !sound.isEmpty else { return }
        locked {
            var free: Int?
            for i in 0..<Self.capacity {
                if voices[i].samples == nil {
                    free = free ?? i
                } else if let loop, voices[i].loop == loop {
                    return
                }
            }
            if let free { voices[free] = Voice(samples: samples, count: sound.count, volume: volume, loop: loop ?? -1) }
        }
    }

    func stopLoop(_ event: Int) {
        locked {
            for i in 0..<Self.capacity where voices[i].samples != nil && voices[i].loop == event { voices[i].samples = nil }
        }
    }

    func stopAll() {
        locked { for i in 0..<Self.capacity { voices[i].samples = nil } }
    }

    /// The audio thread: `frames` of the voices summed into `out`. False: all silence. No allocation here.
    func render(_ frames: Int, into out: UnsafeMutablePointer<Float>) -> Bool {
        out.update(repeating: 0, count: frames)
        os_unfair_lock_lock(lock)
        defer { os_unfair_lock_unlock(lock) }
        var sounding = false
        for i in 0..<Self.capacity {
            guard let samples = voices[i].samples else { continue }
            sounding = true
            let gain = isMuted ? 0 : voices[i].volume
            var done = 0, at = voices[i].at
            while done < frames {
                let run = min(frames - done, voices[i].count - at)
                for j in 0..<run { out[done + j] += samples[at + j] * gain }
                done += run
                at += run
                if at == voices[i].count {
                    if voices[i].loop < 0 {
                        voices[i].samples = nil
                        break
                    }
                    at = 0
                }
            }
            voices[i].at = at
        }
        return sounding
    }
}

/// Sound effects: the game's global sound events (tools/ttre/sfx.py writes out/audio/sfx/<file>.wav and
/// events.tsv: "<event index> <TAB> play|loop|stop <TAB> file <TAB> volume <TAB> stopped event"). The core reports which
/// events started each frame (tc_viewer_sounds). A loop has one voice: retriggered while it plays (the animations restart
/// flight and sliding every cycle) it goes on, until a stop event names it. Other sounds overlap, each to its end (the
/// portal's step-in boofs once per part of Ed going in). How the engine shares voices is not decoded yet. Muted with
/// the music (M), the sounds already playing too.
///
/// play() runs inside the renderer's draw, every frame: all the files are decoded when the table loads and the engine
/// keeps running, so it only ever writes to the Mixer.
final class Sfx {
    nonisolated(unsafe) static let shared = Sfx()

    /// The rate the sounds are decoded to: the highest among the files (nearly all are 22050); the engine's mixer takes it
    /// to the device's.
    private static let sampleRate = 44100.0

    private struct Event { var what: String; var sound: UnsafeBufferPointer<Float>?; var volume: Float; var stops: Int? }
    private var events: [Int: Event] = [:]
    private var sounds: [String: UnsafeBufferPointer<Float>] = [:]  // file -> its decoded samples
    private let mixer = Mixer()
    private let engine = AVAudioEngine()
    private var source: AVAudioSourceNode?
    var muted = false {
        didSet { mixer.muted = muted }
    }
    private var folder: URL?

    /// Reads the event table and the sounds from `audioDir`/sfx (once per folder).
    func load(audioDir: URL) {
        let dir = audioDir.appendingPathComponent("sfx")
        guard dir != folder else { return }
        folder = dir
        mixer.stopAll()  // no voice reads the old samples from here on
        sounds.values.forEach { $0.deallocate() }
        sounds = [:]
        events = [:]
        guard let table = try? String(contentsOf: dir.appendingPathComponent("events.tsv"), encoding: .utf8) else { return }
        let rows = table.split(separator: "\n").filter { !$0.hasPrefix("#") }
            .map { $0.split(separator: "\t", omittingEmptySubsequences: false).map(String.init) }
            .filter { $0.count >= 4 && Int($0[0]) != nil }
        // On all the cores, as the good resampler takes over a second on one and this holds up the first level.
        let files = Array(Set(rows.map { $0[2] }.filter { !$0.isEmpty }))
        var decoded = [UnsafeBufferPointer<Float>?](repeating: nil, count: files.count)
        decoded.withUnsafeMutableBufferPointer { out in
            nonisolated(unsafe) let out = out  // each iteration writes its own element
            DispatchQueue.concurrentPerform(iterations: files.count) { out[$0] = Sfx.decode(dir.appendingPathComponent(files[$0])) }
        }
        for (file, sound) in zip(files, decoded) { sounds[file] = sound }
        for c in rows {
            events[Int(c[0])!] = Event(what: c[1], sound: sounds[c[2]], volume: Float(c[3]) ?? 1, stops: c.count > 4 ? Int(c[4]) : nil)
        }
        startEngine()
    }

    /// A file's samples: mono, at `sampleRate`. Nil: it can't be read (the event stays silent).
    private static func decode(_ url: URL) -> UnsafeBufferPointer<Float>? {
        guard let target = AVAudioFormat(commonFormat: .pcmFormatFloat32, sampleRate: sampleRate, channels: 1, interleaved: false),
              let file = try? AVAudioFile(forReading: url, commonFormat: .pcmFormatFloat32, interleaved: false),
              let input = AVAudioPCMBuffer(pcmFormat: file.processingFormat, frameCapacity: AVAudioFrameCount(file.length)),
              (try? file.read(into: input)) != nil,
              let converter = AVAudioConverter(from: file.processingFormat, to: target) else { return nil }
        converter.downmix = true
        // Not the default: that one dulls what sits near the files' 11 kHz limit, the bells most of all.
        converter.sampleRateConverterAlgorithm = AVSampleRateConverterAlgorithm_Mastering
        converter.sampleRateConverterQuality = AVAudioQuality.max.rawValue
        let ratio = sampleRate / file.processingFormat.sampleRate
        guard let output = AVAudioPCMBuffer(pcmFormat: target, frameCapacity: AVAudioFrameCount(Double(input.frameLength) * ratio) + 4096)
        else { return nil }
        nonisolated(unsafe) var pending: AVAudioPCMBuffer? = input
        var error: NSError?
        let status = converter.convert(to: output, error: &error) { _, state in
            defer { pending = nil }
            state.pointee = pending == nil ? .endOfStream : .haveData
            return pending
        }
        guard status != .error, let samples = output.floatChannelData?[0], output.frameLength > 0 else { return nil }
        let copy = UnsafeMutableBufferPointer<Float>.allocate(capacity: Int(output.frameLength))
        _ = copy.initialize(from: UnsafeBufferPointer(start: samples, count: Int(output.frameLength)))
        return UnsafeBufferPointer(copy)
    }

    private func startEngine() {
        guard source == nil,
              let format = AVAudioFormat(commonFormat: .pcmFormatFloat32, sampleRate: Sfx.sampleRate, channels: 2, interleaved: false)
        else { return }
        let mixer = self.mixer
        let node = AVAudioSourceNode(format: format) { silence, _, frames, list in
            let channels = UnsafeMutableAudioBufferListPointer(list)
            guard channels.count == 2, let left = channels[0].mData?.assumingMemoryBound(to: Float.self),
                  let right = channels[1].mData?.assumingMemoryBound(to: Float.self) else { return kAudioUnitErr_InvalidParameter }
            silence.pointee = ObjCBool(!mixer.render(Int(frames), into: left))
            right.update(from: left, count: Int(frames))
            return noErr
        }
        source = node
        engine.attach(node)
        engine.connect(node, to: engine.mainMixerNode, format: format)
        // A new output device stops the engine, leaving its connections: the voices go on where they were.
        NotificationCenter.default.addObserver(forName: .AVAudioEngineConfigurationChange, object: engine, queue: .main) { _ in
            Sfx.shared.run()
        }
        run()
    }

    private func run() {
        guard !engine.isRunning else { return }
        do {
            try engine.start()
        } catch {
            print("Sound effects: \(error.localizedDescription)")
        }
    }

    func play(_ indices: [UInt16]) {
        for i in indices {
            let k = Int(i)
            guard let e = events[k] else { continue }
            if e.what == "stop" {
                if let target = e.stops { mixer.stopLoop(target) }
            } else if let sound = e.sound {
                mixer.start(sound, volume: e.volume, loop: e.what == "loop" ? k : nil)
            }
        }
    }

    /// Level change, level select: silence everything.
    func stopAll() {
        mixer.stopAll()
    }
}
