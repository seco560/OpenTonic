import AVFoundation

/// Sound effects: the game's global sound events (tools/ttre/sfx.py writes out/audio/sfx/<file>.wav and
/// events.tsv: "<event index> <TAB> play|loop|stop <TAB> file <TAB> volume <TAB> stopped event"). The core reports which
/// events started each frame (tc_viewer_sounds). A loop has one voice: retriggered while it plays (the animations restart
/// flight and sliding every cycle) it goes on, until a stop event names it. Other sounds overlap, each to its end (the
/// portal's step-in boofs once per part of Ed going in). How the engine shares voices is not decoded yet. Muted with
/// the music (M).
final class Sfx {
    nonisolated(unsafe) static let shared = Sfx()

    private struct Event { var what: String; var file: String; var volume: Float; var stops: Int? }
    private var events: [Int: Event] = [:]
    private var data: [String: Data] = [:]      // file -> wav bytes, read once
    private var loops: [Int: AVAudioPlayer] = [:]  // event index -> its looping voice
    private var playing: [AVAudioPlayer] = []      // one-shot sounds, until they end
    private var folder: URL?

    /// Reads the event table from `audioDir`/sfx (once per folder).
    func load(audioDir: URL) {
        let dir = audioDir.appendingPathComponent("sfx")
        guard dir != folder else { return }
        folder = dir
        events = [:]
        guard let table = try? String(contentsOf: dir.appendingPathComponent("events.tsv"), encoding: .utf8) else { return }
        for line in table.split(separator: "\n") where !line.hasPrefix("#") {
            let c = line.split(separator: "\t", omittingEmptySubsequences: false).map(String.init)
            guard c.count >= 4, let k = Int(c[0]) else { continue }
            events[k] = Event(what: c[1], file: c[2], volume: Float(c[3]) ?? 1, stops: c.count > 4 ? Int(c[4]) : nil)
        }
    }

    func play(_ indices: [UInt16]) {
        for i in indices {
            let k = Int(i)
            guard let e = events[k] else { continue }
            if e.what == "stop" {
                if let target = e.stops { loops.removeValue(forKey: target)?.stop() }
                continue
            }
            if e.what == "loop", loops[k]?.isPlaying == true { continue }
            playing.removeAll { !$0.isPlaying }
            guard !e.file.isEmpty, let folder,
                  let bytes = data[e.file] ?? (try? Data(contentsOf: folder.appendingPathComponent(e.file))) else { continue }
            data[e.file] = bytes
            guard let p = try? AVAudioPlayer(data: bytes) else { continue }
            p.volume = Music.shared.muted ? 0 : e.volume
            if e.what == "loop" {
                p.numberOfLoops = -1
                loops[k] = p
            } else {
                playing.append(p)
            }
            p.play()
        }
    }

    /// Level change, level select: silence everything.
    func stopAll() {
        loops.values.forEach { $0.stop() }
        playing.forEach { $0.stop() }
        loops = [:]
        playing = []
    }
}
