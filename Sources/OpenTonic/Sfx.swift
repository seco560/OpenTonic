import AVFoundation
import os
import TonicCore

/// The voices, mixed on the audio thread: the running engine pulls them, so starting a sound is only a write to this
/// table. Nothing here may wait on the audio system: AVAudioPlayer's play() blocks its caller for 16 ms or more, which
/// from inside the renderer's draw is a dropped frame for every footstep. (AVAudioPlayerNode's play() and stop() take
/// 11 ms.) As the SDL build's sfx.c. Each voice has the original mixer's gains (the core's tc_viewer_sound_mix: where it
/// is heard from, docs/ENGINE.md "3D sound") and its pitch; with Dolby Surround on, the voices' rear signal goes through
/// the original's encoder (tc_sound_dolby_filter) into the two channels.
private final class Mixer: @unchecked Sendable {
    struct Voice {
        var samples: UnsafePointer<Float>?  // nil: free. Mono, at Sfx.sampleRate; owned by Sfx
        var count = 0
        var at = 0
        var frac = 0.0  // between samples: the pitch's steps (the original's mixer takes the sample it lands on)
        var pitch = 1.0
        var left: Float = 0, right: Float = 0, rear: Float = 0  // its gains now
        var loop = -1  // the loop event it plays, -1 = a one-shot sound
        var speech = false  // a character's line: dismissing it stops it
        var source = tc_sound_source()  // where it sounds from
        var volume: Float = 1  // its event's (events.tsv)
        var follow = false  // placed again every frame (events.tsv's follow column)
        var serial: UInt32 = 0  // which start this slot holds (the frame's update writes only to the same one)
    }

    static let capacity = 64  // the original's software mixer's voices (WAVMWSVR.DLL: 0x40); all busy, a sound isn't played
    private let voices = UnsafeMutablePointer<Voice>.allocate(capacity: capacity)
    private let lock = os_unfair_lock_t.allocate(capacity: 1)
    private var isMuted = false
    private var isDolby = false

    // The Dolby encoder: its filter made for this rate (the original's 32 taps at 22050 Hz: as long a span), the rear
    // signal of the block and the taps' worth before it.
    private static let taps = 64
    private static let block = 4096
    private let coef = UnsafeMutablePointer<Float>.allocate(capacity: taps)
    private let rear = UnsafeMutablePointer<Float>.allocate(capacity: taps + block)

    init(rate: Double) {
        voices.initialize(repeating: Voice(), count: Self.capacity)
        lock.initialize(to: os_unfair_lock())
        var c = [Int16](repeating: 0, count: Self.taps)
        tc_sound_dolby_filter(&c, Int32(Self.taps), Float(rate))
        for k in 0..<Self.taps { coef[k] = Float(c[k]) / 8192 }  // its sum >> 13
        rear.initialize(repeating: 0, count: Self.taps + Self.block)
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

    var dolby: Bool {
        get { locked { isDolby } }
        set { locked { isDolby = newValue } }
    }

    private var serials: UInt32 = 0

    /// A free voice plays `v.samples`; all busy, it isn't played. A loop (v.loop: its event) already playing from the same
    /// emitter goes on instead (the animations restart flight and sliding every cycle).
    func start(_ v: Voice) {
        guard v.samples != nil, v.count > 0 else { return }
        locked {
            var free: Int?
            for i in 0..<Self.capacity {
                if voices[i].samples == nil {
                    free = free ?? i
                } else if v.loop >= 0, voices[i].loop == v.loop, voices[i].source.emitter == v.source.emitter {
                    return
                }
            }
            guard let free else { return }
            serials &+= 1
            voices[free] = v
            voices[free].serial = serials
        }
    }

    /// A stop event: its loop, from that emitter (any, when the stop has none: a global one).
    func stopLoop(_ event: Int, from source: tc_sound_source) {
        locked {
            for i in 0..<Self.capacity where voices[i].samples != nil && voices[i].loop == event &&
                (source.kind == UInt8(TC_SOUND_FLAT) || voices[i].source.emitter == source.emitter) {
                voices[i].samples = nil
            }
        }
    }

    /// The playing voices heard from somewhere (all: the flat ones too), as (slot, serial, voice): to place again outside
    /// the lock.
    func placed(all: Bool, into out: inout [(Int, UInt32, Voice)]) {
        out.removeAll(keepingCapacity: true)
        locked {
            for i in 0..<Self.capacity where voices[i].samples != nil &&
                (all || voices[i].source.kind != UInt8(TC_SOUND_FLAT)) {
                out.append((i, voices[i].serial, voices[i]))
            }
        }
    }

    /// New gains for a slot, if it still holds that start; nil: it ends.
    func set(_ slot: Int, serial: UInt32, _ mix: tc_sound_mix?) {
        locked {
            guard voices[slot].samples != nil, voices[slot].serial == serial else { return }
            guard let mix else { voices[slot].samples = nil; return }
            voices[slot].left = mix.left
            voices[slot].right = mix.right
            voices[slot].rear = mix.rear
        }
    }

    func stopSpeech() {
        locked { for i in 0..<Self.capacity where voices[i].speech { voices[i].samples = nil } }
    }

    func stopAll() {
        locked { for i in 0..<Self.capacity { voices[i].samples = nil } }
    }

    /// The audio thread: `frames` of the voices summed into `left` and `right`. False: all silence. No allocation here.
    func render(_ frames: Int, left: UnsafeMutablePointer<Float>, right: UnsafeMutablePointer<Float>) -> Bool {
        os_unfair_lock_lock(lock)
        defer { os_unfair_lock_unlock(lock) }
        var sounding = false, done = 0
        while done < frames {  // in blocks the rear signal holds
            let n = min(frames - done, Self.block)
            sounding = mix(n, left: left + done, right: right + done) || sounding
            done += n
        }
        return sounding
    }

    private func mix(_ frames: Int, left: UnsafeMutablePointer<Float>, right: UnsafeMutablePointer<Float>) -> Bool {
        let t = Self.taps, back = rear + t  // back[-t ..< 0]: the last block's end
        left.update(repeating: 0, count: frames)
        right.update(repeating: 0, count: frames)
        back.update(repeating: 0, count: frames)
        var sounding = false
        for i in 0..<Self.capacity {
            guard let samples = voices[i].samples else { continue }
            sounding = true
            let gl = isMuted ? 0 : voices[i].left, gr = isMuted ? 0 : voices[i].right
            let gb = isMuted || !isDolby ? 0 : voices[i].rear
            let count = voices[i].count, step = voices[i].pitch, heard = gl != 0 || gr != 0 || gb != 0
            var at = voices[i].at, frac = voices[i].frac
            for j in 0..<frames {
                if at >= count {
                    if voices[i].loop < 0 {
                        voices[i].samples = nil
                        break
                    }
                    at -= count
                }
                if heard {  // an unheard voice runs on silently (the original's mixer: under 3, not mixed)
                    let x = samples[at]
                    left[j] += x * gl
                    right[j] += x * gr
                    back[j] += x * gb
                }
                if step == 1 {
                    at += 1
                } else {
                    frac += step
                    let whole = Int(frac)
                    at += whole
                    frac -= Double(whole)
                }
            }
            voices[i].at = at
            voices[i].frac = frac
        }
        if isDolby {  // the encoder (WAVMWSVR.DLL 0x1000d823): y[n] = sum c[k] rear[n - taps + k], to the left, from the right
            for j in 0..<frames {
                var y: Float = 0
                for k in 0..<t { y += coef[k] * back[j - t + k] }
                left[j] += y
                right[j] -= y
            }
            rear.update(from: back + frames - t, count: t)  // the next block's history
        } else {
            rear.update(repeating: 0, count: t)
        }
        return sounding
    }
}

/// Sound effects: the game's sound events (tools/ttre/sfx.py writes out/audio/sfx/<file>.wav and events.tsv:
/// "<event index> <TAB> play|loop|stop|voice <TAB> file <TAB> volume <TAB> stopped event <TAB> follow <TAB> pitch
/// spread"). The core reports which events started each frame and where each sounds from (tc_viewer_sounds,
/// tc_viewer_sound_sources); its gains come from tc_viewer_sound_mix, at the start and, for the events that follow
/// their emitter, every frame (update): the original's 3D sound and mixer (docs/ENGINE.md "3D sound"), its pitch spread,
/// and its Dolby Surround (J, kept across launches in UserDefaults "dolbySurround"; off by default, as the original's
/// sound menu has it). A loop has one voice per emitter: retriggered while it plays (the animations restart flight and
/// sliding every cycle) it goes on, until a stop event names it. Other sounds overlap, each to its end (the portal's
/// step-in boofs once per part of Ed going in). Muted with the music (M), the sounds already playing too.
///
/// play() runs inside the renderer's draw, every frame: all the files are decoded when the table loads and the engine
/// keeps running, so it only ever writes to the Mixer.
final class Sfx {
    nonisolated(unsafe) static let shared = Sfx()

    /// The rate the sounds are decoded to: the highest among the files (nearly all are 22050); the engine's mixer takes it
    /// to the device's.
    private static let sampleRate = 44100.0

    private static let voiceStop = 8191  // the core's TC_AI_VOICE_STOP: a line was dismissed
    private var speech: UnsafeBufferPointer<Float>?  // the line being said: the voices are decoded when said (163 of them)
    private struct Event {
        var file = ""; var what: String; var sound: UnsafeBufferPointer<Float>?; var volume: Float; var stops: Int?; var follow = false
        var spread: Float = 0
    }
    private var table: [Int: Event] = [:]
    private var sounds: [String: UnsafeBufferPointer<Float>] = [:]  // file -> its decoded samples
    private let mixer = Mixer(rate: Sfx.sampleRate)
    private var regain = false  // the Dolby switch changed: every voice's gains again at the next update
    private var placing: [(Int, UInt32, Mixer.Voice)] = []
    private let engine = AVAudioEngine()
    private var source: AVAudioSourceNode?
    var muted = false {
        didSet { mixer.muted = muted }
    }
    /// Dolby Surround (the original's sound menu option): J toggles it, kept across launches.
    var dolby = UserDefaults.standard.bool(forKey: "dolbySurround") {
        didSet {
            mixer.dolby = dolby
            regain = true
            UserDefaults.standard.set(dolby, forKey: "dolbySurround")
        }
    }
    private var folder: URL?

    /// Reads the event table and the sounds from `audioDir`/sfx (once per folder).
    func load(audioDir: URL) {
        let dir = audioDir.appendingPathComponent("sfx")
        guard dir != folder else { return }
        folder = dir
        mixer.stopAll()  // no voice reads the old samples from here on
        sounds.values.forEach { $0.deallocate() }
        speech?.deallocate()
        speech = nil
        sounds = [:]
        table = [:]
        guard let text = try? String(contentsOf: dir.appendingPathComponent("events.tsv"), encoding: .utf8) else { return }
        let rows = text.split(separator: "\n").filter { !$0.hasPrefix("#") }
            .map { $0.split(separator: "\t", omittingEmptySubsequences: false).map(String.init) }
            .filter { $0.count >= 4 && Int($0[0]) != nil }
        // On all the cores, as the good resampler takes over a second on one and this holds up the first level.
        let files = Array(Set(rows.filter { $0[1] != "voice" }.map { $0[2] }.filter { !$0.isEmpty }))
        var decoded = [UnsafeBufferPointer<Float>?](repeating: nil, count: files.count)
        decoded.withUnsafeMutableBufferPointer { out in
            nonisolated(unsafe) let out = out  // each iteration writes its own element
            DispatchQueue.concurrentPerform(iterations: files.count) { out[$0] = Sfx.decode(dir.appendingPathComponent(files[$0])) }
        }
        for (file, sound) in zip(files, decoded) { sounds[file] = sound }
        for c in rows {
            table[Int(c[0])!] = Event(file: c[2], what: c[1], sound: sounds[c[2]], volume: Float(c[3]) ?? 1,
                                       stops: c.count > 4 ? Int(c[4]) : nil, follow: c.count > 5 && c[5] == "1",
                                       spread: c.count > 6 ? Float(c[6]) ?? 0 : 0)
        }
        mixer.dolby = dolby
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
            silence.pointee = ObjCBool(!mixer.render(Int(frames), left: left, right: right))
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

    /// The sounds the viewer's last update started.
    func play(_ v: UnsafeMutablePointer<tc_viewer>) {
        var events: UnsafePointer<UInt16>?
        let n = Int(tc_viewer_sounds(v, &events))
        guard n > 0, let events, let sources = tc_viewer_sound_sources(v) else { return }
        for i in 0..<n {
            let k = Int(events[i]), source = sources[i]
            if k == Self.voiceStop { mixer.stopSpeech(); continue }
            guard let e = table[k] else { continue }
            var voice = Mixer.Voice(source: source, volume: e.volume, follow: e.follow)
            voice.pitch = Double(tc_sound_pitch(e.spread, Int32.random(in: 0...32767)))
            if e.what == "voice" {
                mixer.stopSpeech()  // no voice reads the old samples from here on
                speech?.deallocate()
                speech = folder.flatMap { e.file.isEmpty ? nil : Sfx.decode($0.appendingPathComponent(e.file)) }
                guard let speech, let base = speech.baseAddress else { continue }
                voice.samples = base
                voice.count = speech.count
                voice.speech = true
            } else if e.what == "stop" {
                if let target = e.stops { mixer.stopLoop(target, from: source) }
                continue
            } else if let sound = e.sound, let base = sound.baseAddress {
                voice.samples = base
                voice.count = sound.count
                voice.loop = e.what == "loop" ? k : -1
            } else { continue }
            var mix = tc_sound_mix()
            let alive = withUnsafePointer(to: source) { tc_viewer_sound_mix(v, $0, e.volume, dolby, &mix) }
            if !alive { continue }  // its perso was already taken out
            voice.left = mix.left
            voice.right = mix.right
            voice.rear = mix.rear
            mixer.start(voice)
        }
    }

    /// Once a frame, after the viewer's update: the playing sounds heard from somewhere, placed again where their
    /// emitters are now (those whose events follow them), or stopped (a perso's own, the perso taken out since).
    func update(_ v: UnsafeMutablePointer<tc_viewer>) {
        let all = regain
        regain = false
        mixer.placed(all: all, into: &placing)
        for (slot, serial, voice) in placing {
            var mix = tc_sound_mix()
            let alive = withUnsafePointer(to: voice.source) { tc_viewer_sound_mix(v, $0, voice.volume, dolby, &mix) }
            if !alive {
                mixer.set(slot, serial: serial, nil)
            } else if voice.follow || all {
                mixer.set(slot, serial: serial, mix)
            }
        }
    }

    /// Level change, level select: silence everything.
    func stopAll() {
        mixer.stopAll()
    }
}
