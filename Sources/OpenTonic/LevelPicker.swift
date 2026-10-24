import AppKit
import TonicCore

/// Grid of converted levels (*.ttlv in one folder), each shown with its loading-screen image
/// from out/Vignette/Level/<Name>.png when available (matched case-insensitively).
/// Three sections under headings: "Levels", the game's cutscene-only levels (`tc_level_is_cutscene`), and
/// "Imported": models brought in with tools/ttre/import_gltf.py (they have a folder out/Imported/<name>), shown with a
/// question mark as their picture.
final class LevelPicker: NSView {
    struct Entry { let name: String; let url: URL; let image: NSImage?; let section: Int }

    private static let titles = ["Levels", "Cutscenes", "Imported"]
    private let entries: [Entry]
    private let onPick: (URL) -> Void
    /// The big "Play Intro Video" button at the top (the movie then starts the ski slope, as the original).
    var onIntro: (() -> Void)?
    private let introButton = NSButton(title: "Play Intro Video", target: nil, action: nil)
    private let introHeight: CGFloat = 64
    private var tiles: [NSButton] = []
    private let headings = LevelPicker.titles.map { NSTextField(labelWithString: $0) }
    private let rules = LevelPicker.titles.map { _ in NSBox() }
    private let headingHeight: CGFloat = 40
    private let tile = NSSize(width: 256, height: 192)
    private let caption: CGFloat = 26, pad: CGFloat = 20

    override var isFlipped: Bool { true }

    init(folder: URL, thumbnails: URL, onPick: @escaping (URL) -> Void) {
        self.onPick = onPick
        let fm = FileManager.default
        let imported = folder.deletingLastPathComponent().appendingPathComponent("Imported")
        func section(_ url: URL) -> Int {
            let name = url.deletingPathExtension().lastPathComponent
            if fm.fileExists(atPath: imported.appendingPathComponent(name).path) { return 2 }
            return tc_level_is_cutscene(name) != 0 ? 1 : 0
        }
        let files: [URL] = ((try? fm.contentsOfDirectory(at: folder, includingPropertiesForKeys: nil)) ?? [])
            .filter { $0.pathExtension.lowercased() == "ttlv" }
        let tagged: [(URL, Int)] = files.map { ($0, section($0)) }
        let levels = tagged.sorted { (a: (URL, Int), b: (URL, Int)) -> Bool in  // by section, then by name
            if a.1 != b.1 { return a.1 < b.1 }
            return a.0.lastPathComponent.localizedCaseInsensitiveCompare(b.0.lastPathComponent) == .orderedAscending
        }
        let thumbs = Dictionary(((try? fm.contentsOfDirectory(at: thumbnails, includingPropertiesForKeys: nil)) ?? [])
            .filter { $0.pathExtension.lowercased() == "png" }
            .map { ($0.deletingPathExtension().lastPathComponent.lowercased(), $0) }, uniquingKeysWith: { a, _ in a })
        let unknown = LevelPicker.questionMark(size: tile)
        entries = levels.map { url, s in
            let name = url.deletingPathExtension().lastPathComponent
            let image = thumbs[name.lowercased()].flatMap { NSImage(contentsOf: $0) } ?? (s == 2 ? unknown : nil)
            return Entry(name: name, url: url, image: image, section: s)
        }
        super.init(frame: .zero)
        for (i, e) in entries.enumerated() {
            let b = NSButton(title: e.name, image: e.image ?? NSImage(size: tile), target: self, action: #selector(pick(_:)))
            b.tag = i
            b.imagePosition = .imageAbove
            b.imageScaling = .scaleProportionallyUpOrDown
            b.isBordered = false
            b.font = .systemFont(ofSize: 14, weight: .medium)
            b.toolTip = e.url.path
            addSubview(b)
            tiles.append(b)
        }
        introButton.bezelStyle = .rounded
        introButton.controlSize = .large
        introButton.font = .systemFont(ofSize: 20, weight: .semibold)
        introButton.target = self
        introButton.action = #selector(intro)
        introButton.keyEquivalent = ""
        introButton.toolTip = "The game's intro movie (V)"
        addSubview(introButton)
        for (k, (heading, rule)) in zip(headings, rules).enumerated() where entries.contains(where: { $0.section == k }) {
            heading.font = .systemFont(ofSize: 15, weight: .semibold)
            heading.textColor = .secondaryLabelColor
            rule.boxType = .separator
            addSubview(heading)
            addSubview(rule)
        }
    }

    required init?(coder: NSCoder) { fatalError("not used") }

    var isEmpty: Bool { entries.isEmpty }

    /// The placeholder picture of an imported level: a big question mark on a dark tile.
    private static func questionMark(size: NSSize) -> NSImage {
        NSImage(size: size, flipped: false) { r in
            NSColor(calibratedRed: 0.14, green: 0.15, blue: 0.22, alpha: 1).setFill()
            NSBezierPath(roundedRect: r, xRadius: 10, yRadius: 10).fill()
            let text = NSAttributedString(string: "?", attributes: [
                .font: NSFont.systemFont(ofSize: r.height * 0.6, weight: .bold),
                .foregroundColor: NSColor(calibratedRed: 0.55, green: 0.6, blue: 0.75, alpha: 1)])
            let s = text.size()
            text.draw(at: NSPoint(x: (r.width - s.width) / 2, y: (r.height - s.height) / 2))
            return true
        }
    }

    /// Lay the tiles out for the given width and size self to fit (we live in a scroll view).
    func relayout(width: CGFloat) {
        let cellW = tile.width + pad, cellH = tile.height + caption + pad
        let cols = max(1, Int((width - pad) / cellW))
        let left = max(pad, (width - CGFloat(cols) * cellW + pad) / 2)
        let rowWidth = CGFloat(cols) * cellW - pad
        introButton.sizeToFit()
        let bw = max(introButton.frame.width + 60, 320)
        introButton.frame = NSRect(x: (width - bw) / 2, y: pad + (introHeight - introButton.frame.height) / 2 - 8, width: bw,
                                   height: introButton.frame.height)
        // Each section starts on a new row, under its heading; empty sections take no space.
        var y = pad + introHeight
        for (k, (heading, rule)) in zip(headings, rules).enumerated() {
            let members = entries.indices.filter { entries[$0].section == k }
            guard !members.isEmpty else { continue }
            heading.sizeToFit()
            heading.frame.origin = NSPoint(x: left, y: y + 4)
            rule.frame = NSRect(x: left, y: y + headingHeight - 10, width: rowWidth, height: 1)
            y += headingHeight
            for (j, i) in members.enumerated() {
                tiles[i].frame = NSRect(x: left + CGFloat(j % cols) * cellW, y: y + CGFloat(j / cols) * cellH,
                                        width: tile.width, height: tile.height + caption)
            }
            y += CGFloat((members.count + cols - 1) / cols) * cellH
        }
        setFrameSize(NSSize(width: width, height: y))
    }

    @objc private func pick(_ sender: NSButton) { onPick(entries[sender.tag].url) }
    @objc private func intro() { onIntro?() }
}
