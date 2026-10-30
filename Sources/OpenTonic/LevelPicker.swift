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
    /// The two big buttons at the top: "Play Intro Video" (the movie then starts the ski slope, as the original) and
    /// "Demo" (Ed wanders around random main levels by himself).
    var onIntro: (() -> Void)?
    var onDemo: (() -> Void)?
    private let introButton = PickerButton(title: "▶  Play Intro Video", colors: (NSColor(calibratedRed: 0.95, green: 0.45, blue: 0.2, alpha: 1),
                                                                         NSColor(calibratedRed: 0.8, green: 0.15, blue: 0.45, alpha: 1)))
    private let demoButton = PickerButton(title: "✦  Demo", colors: (NSColor(calibratedRed: 0.2, green: 0.75, blue: 0.85, alpha: 1),
                                                               NSColor(calibratedRed: 0.25, green: 0.3, blue: 0.85, alpha: 1)))
    private let introHeight: CGFloat = 84
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
        // The screen is dark whatever the system's appearance: light mode's black text would vanish on it.
        appearance = NSAppearance(named: .darkAqua)
        let shadow = NSShadow()
        shadow.shadowColor = NSColor(white: 0, alpha: 0.9)
        shadow.shadowOffset = NSSize(width: 0, height: -1)
        shadow.shadowBlurRadius = 3
        for (i, e) in entries.enumerated() {
            let b = NSButton(title: e.name, image: e.image ?? NSImage(size: tile), target: self, action: #selector(pick(_:)))
            b.tag = i
            b.imagePosition = .imageAbove
            b.imageScaling = .scaleProportionallyUpOrDown
            b.isBordered = false
            b.attributedTitle = NSAttributedString(string: e.name, attributes: [
                .font: NSFont.systemFont(ofSize: 15, weight: .semibold), .foregroundColor: NSColor.white, .shadow: shadow])
            b.toolTip = e.url.path
            addSubview(b)
            tiles.append(b)
        }
        introButton.target = self
        introButton.action = #selector(intro)
        introButton.toolTip = "The game's intro movie (V)"
        demoButton.target = self
        demoButton.action = #selector(demo)
        demoButton.toolTip = "Ed wanders around random levels by himself; any key or click ends it (D)"
        addSubview(introButton)
        addSubview(demoButton)
        for (k, (heading, rule)) in zip(headings, rules).enumerated() where entries.contains(where: { $0.section == k }) {
            heading.font = .systemFont(ofSize: 17, weight: .bold)
            heading.textColor = NSColor(white: 0.92, alpha: 1)
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
        let bw: CGFloat = 280, bh: CGFloat = 54, gap: CGFloat = 24
        let bx = (width - 2 * bw - gap) / 2, by = pad + (introHeight - bh) / 2 - 8
        introButton.frame = NSRect(x: bx, y: by, width: bw, height: bh)
        demoButton.frame = NSRect(x: bx + bw + gap, y: by, width: bw, height: bh)
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
    @objc private func demo() { onDemo?() }
}

/// A big pill-shaped button of the level select: a two-colour gradient, white bold title, lighter under the mouse,
/// darker while pressed.
final class PickerButton: NSButton {
    private let colors: (NSColor, NSColor)
    private var hovering = false { didSet { needsDisplay = true } }

    init(title: String, colors: (NSColor, NSColor)) {
        self.colors = colors
        super.init(frame: .zero)
        self.title = title
        isBordered = false
        setButtonType(.momentaryPushIn)
        focusRingType = .none
    }

    required init?(coder: NSCoder) { fatalError("not used") }

    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        trackingAreas.forEach(removeTrackingArea)
        addTrackingArea(NSTrackingArea(rect: bounds, options: [.mouseEnteredAndExited, .activeInKeyWindow], owner: self))
    }

    override func mouseEntered(with event: NSEvent) { hovering = true }
    override func mouseExited(with event: NSEvent) { hovering = false }
    override func resetCursorRects() { addCursorRect(bounds, cursor: .pointingHand) }

    override func draw(_ dirtyRect: NSRect) {
        let r = bounds.insetBy(dx: 2, dy: 2), path = NSBezierPath(roundedRect: r, xRadius: r.height / 2, yRadius: r.height / 2)
        let pressed = isHighlighted
        func tone(_ c: NSColor) -> NSColor {
            pressed ? c.blended(withFraction: 0.3, of: .black)! : hovering ? c.blended(withFraction: 0.2, of: .white)! : c
        }
        NSGraphicsContext.saveGraphicsState()
        let glow = NSShadow()
        glow.shadowColor = colors.1.withAlphaComponent(hovering ? 0.8 : 0.45)
        glow.shadowBlurRadius = hovering ? 14 : 8
        glow.set()
        NSGradient(starting: tone(colors.0), ending: tone(colors.1))?.draw(in: path, angle: 0)
        NSGraphicsContext.restoreGraphicsState()
        NSGradient(starting: NSColor(white: 1, alpha: 0), ending: NSColor(white: 1, alpha: 0.28))?  // gloss on the top half
            .draw(in: NSBezierPath(roundedRect: NSRect(x: r.minX, y: r.midY, width: r.width, height: r.height / 2),
                                   xRadius: r.height / 4, yRadius: r.height / 4), angle: 90)
        NSColor(white: 1, alpha: 0.35).setStroke()
        path.lineWidth = 1
        path.stroke()
        let shadow = NSShadow()
        shadow.shadowColor = NSColor(white: 0, alpha: 0.6)
        shadow.shadowOffset = NSSize(width: 0, height: -1)
        shadow.shadowBlurRadius = 2
        let text = NSAttributedString(string: title, attributes: [
            .font: NSFont.systemFont(ofSize: 20, weight: .bold), .foregroundColor: NSColor.white, .shadow: shadow])
        let size = text.size()
        text.draw(at: NSPoint(x: r.midX - size.width / 2, y: r.midY - size.height / 2))
    }
}
