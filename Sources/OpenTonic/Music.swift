import AVFoundation

/// Level music for the viewer. The level -> track mapping is a user-edited table, because the game
/// picks music at runtime (sound events/AI, not yet decoded):
///   out/audio/music.tsv  lines "<level name><TAB><file in out/audio/music>", '#' comments.
/// Tracks are extracted from the sound banks with vgmstream (see docs/NOTES.md, Audio).
final class Music {
    nonisolated(unsafe) static let shared = Music()

    private var player: AVAudioPlayer?
    private(set) var current: String?
    var muted = false { didSet { player?.volume = muted ? 0 : 1 } }

    /// Plays the track mapped to `level` (looping), or stops if there is none. `audioDir` = out/audio.
    /// The level picker uses the entry named "menu". A track that is already playing just continues.
    func play(level: String, audioDir: URL) {
        let file = track(for: level, audioDir: audioDir)
        if let file, file == current, player?.isPlaying == true { return }
        stop()
        guard let file else { return }
        let url = audioDir.appendingPathComponent("music").appendingPathComponent(file)
        guard let p = try? AVAudioPlayer(contentsOf: url) else {
            print("Music: cannot open \(url.path)")
            return
        }
        p.numberOfLoops = -1
        p.volume = muted ? 0 : 1
        p.play()
        player = p
        current = file
    }

    private func track(for level: String, audioDir: URL) -> String? {
        guard let table = try? String(contentsOf: audioDir.appendingPathComponent("music.tsv"), encoding: .utf8) else { return nil }
        for line in table.split(separator: "\n") where !line.hasPrefix("#") {
            let cols = line.split(separator: "\t").map { $0.trimmingCharacters(in: .whitespaces) }
            if cols.count >= 2, !cols[1].isEmpty, cols[0].caseInsensitiveCompare(level) == .orderedSame { return cols[1] }
        }
        return nil
    }

    func stop() {
        player?.stop()
        player = nil
        current = nil
    }
}
