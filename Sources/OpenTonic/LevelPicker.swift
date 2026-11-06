import AppKit
import QuartzCore
import TonicCore

/// Grid of converted levels (*.ttlv in the levels folder, and in the Special Edition's out/se/ttlv), each shown with
/// its loading-screen image from out/Vignette/Level/<Name>.png (the Special Edition's: out/se/Vignette/Level) when
/// available (matched case-insensitively).
/// Four sections: "Retail Edition" and "Special Edition" (the 1998 disc's levels, tools/ttre/se.py) are two panes side
/// by side, one showing at a time: fixed height, each scrolling inside itself, slid left and right with the arrows at
/// their top (retail is the left one; side by side because portals will link the two). Under them the page goes on with
/// the game's cutscene-only levels (`tc_level_is_cutscene`) and "Imported": models brought in with
/// tools/ttre/import_gltf.py (they have a folder out/Imported/<name>), shown with a question mark as their picture.
final class LevelPicker: NSView {
    struct Entry { let name: String; let url: URL; let image: NSImage?; let section: Int }
    private struct Pane { let section: Int; let scroll: NSScrollView; let grid: NSView }

    private static let titles = ["Retail Edition", "Special Edition", "Cutscenes", "Imported"]
    private static let specialEdition = 1, imported = 3
    private static let editions = 2  // sections 0 and 1 are the panes
    private let entries: [Entry]
    private let onPick: (URL) -> Void
    /// The two big buttons at the top: "Play Intro Video" (the movie then starts the ski slope, as the original) and
    /// "Replays" (the recordings: ReplaysScreen).
    var onIntro: (() -> Void)?
    var onReplays: (() -> Void)?
    private let introButton = PickerButton(title: "▶  Play Intro Video", colors: (NSColor(calibratedRed: 0.95, green: 0.45, blue: 0.2, alpha: 1),
                                                                         NSColor(calibratedRed: 0.8, green: 0.15, blue: 0.45, alpha: 1)))
    private let replaysButton = PickerButton(title: "✦  Replays", colors: (NSColor(calibratedRed: 0.2, green: 0.75, blue: 0.85, alpha: 1),
                                                               NSColor(calibratedRed: 0.25, green: 0.3, blue: 0.85, alpha: 1)))
    private let leftArrow = PickerButton(title: "◀", colors: LevelPicker.arrowColors)
    private let rightArrow = PickerButton(title: "▶", colors: LevelPicker.arrowColors)
    private static let arrowColors = (NSColor(calibratedRed: 0.34, green: 0.38, blue: 0.56, alpha: 1),
                                      NSColor(calibratedRed: 0.22, green: 0.25, blue: 0.4, alpha: 1))
    private let introHeight: CGFloat = 84
    private var tiles: [NSButton] = []
    /// The window onto the panes (the full width, so a pane leaves by the window's side), and the strip that slides in it.
    private let viewport = FlippedView(), strip = FlippedView()
    private var panes: [Pane] = []
    private var pane = 0  // the one showing, in `panes`
    private let headings = LevelPicker.titles.map { NSTextField(labelWithString: $0) }
    private let rules = LevelPicker.titles.map { _ in NSBox() }
    private let headingHeight: CGFloat = 40
    private let tile = NSSize(width: 256, height: 192)
    private let caption: CGFloat = 26, pad: CGFloat = 20

    override var isFlipped: Bool { true }

    /// showSpecial: the Special Edition's pane first (when it has levels).
    init(folder: URL, thumbnails: URL, seFolder: URL? = nil, seThumbnails: URL? = nil, showSpecial: Bool = false,
         onPick: @escaping (URL) -> Void) {
        self.onPick = onPick
        let fm = FileManager.default
        let imported = folder.deletingLastPathComponent().appendingPathComponent("Imported")
        func section(_ url: URL) -> Int {
            let name = url.deletingPathExtension().lastPathComponent
            if fm.fileExists(atPath: imported.appendingPathComponent(name).path) { return LevelPicker.imported }
            return tc_level_is_cutscene(name) != 0 ? 2 : 0
        }
        func levels(in dir: URL?) -> [URL] {
            guard let dir else { return [] }
            return ((try? fm.contentsOfDirectory(at: dir, includingPropertiesForKeys: nil)) ?? [])
                .filter { $0.pathExtension.lowercased() == "ttlv" }
        }
        let tagged: [(URL, Int)] = levels(in: folder).map { ($0, section($0)) }
            + levels(in: seFolder).map { ($0, LevelPicker.specialEdition) }
        let levels = tagged.sorted { (a: (URL, Int), b: (URL, Int)) -> Bool in  // by section, then by name
            if a.1 != b.1 { return a.1 < b.1 }
            return a.0.lastPathComponent.localizedCaseInsensitiveCompare(b.0.lastPathComponent) == .orderedAscending
        }
        func pictures(_ dir: URL?) -> [String: URL] {
            guard let dir else { return [:] }
            return Dictionary(((try? fm.contentsOfDirectory(at: dir, includingPropertiesForKeys: nil)) ?? [])
                .filter { $0.pathExtension.lowercased() == "png" }
                .map { ($0.deletingPathExtension().lastPathComponent.lowercased(), $0) }, uniquingKeysWith: { a, _ in a })
        }
        let thumbs = pictures(thumbnails), seThumbs = pictures(seThumbnails)
        let unknown = LevelPicker.questionMark(size: tile)
        entries = levels.map { url, s in
            let name = url.deletingPathExtension().lastPathComponent
            let picture = (s == LevelPicker.specialEdition ? seThumbs : thumbs)[name.lowercased()]
            let image = picture.flatMap { NSImage(contentsOf: $0) } ?? (s == LevelPicker.imported ? unknown : nil)
            return Entry(name: name, url: url, image: image, section: s)
        }
        super.init(frame: .zero)
        // The screen is dark whatever the system's appearance: light mode's black text would vanish on it.
        appearance = NSAppearance(named: .darkAqua)
        let shadow = NSShadow()
        shadow.shadowColor = NSColor(white: 0, alpha: 0.9)
        shadow.shadowOffset = NSSize(width: 0, height: -1)
        shadow.shadowBlurRadius = 3
        viewport.wantsLayer = true
        viewport.clipsToBounds = true
        viewport.addSubview(strip)
        addSubview(viewport)
        for k in 0..<LevelPicker.editions where entries.contains(where: { $0.section == k }) {
            let scroll = PaneScrollView(), grid = FlippedView()
            scroll.hasVerticalScroller = true
            scroll.drawsBackground = false
            scroll.automaticallyAdjustsContentInsets = false
            scroll.verticalScrollElasticity = .none
            scroll.documentView = grid
            strip.addSubview(scroll)
            panes.append(Pane(section: k, scroll: scroll, grid: grid))
        }
        pane = panes.firstIndex { $0.section == LevelPicker.specialEdition && showSpecial } ?? 0
        for (i, e) in entries.enumerated() {
            let b = NSButton(title: e.name, image: e.image ?? NSImage(size: tile), target: self, action: #selector(pick(_:)))
            b.tag = i
            b.imagePosition = .imageAbove
            b.imageScaling = .scaleProportionallyUpOrDown
            b.isBordered = false
            b.attributedTitle = NSAttributedString(string: e.name, attributes: [
                .font: NSFont.systemFont(ofSize: 15, weight: .semibold), .foregroundColor: NSColor.white, .shadow: shadow])
            b.toolTip = e.url.path
            let parent: NSView = panes.first { $0.section == e.section }?.grid ?? self
            parent.addSubview(b)
            tiles.append(b)
        }
        introButton.target = self
        introButton.action = #selector(intro)
        introButton.toolTip = "The game's intro movie (V)"
        replaysButton.target = self
        replaysButton.action = #selector(replays)
        replaysButton.toolTip = "Your recorded sessions: play or delete them (R)"
        leftArrow.target = self
        leftArrow.action = #selector(slideLeft)
        leftArrow.toolTip = "Retail Edition (left arrow key)"
        rightArrow.target = self
        rightArrow.action = #selector(slideRight)
        rightArrow.toolTip = "Special Edition (right arrow key)"
        addSubview(introButton)
        addSubview(replaysButton)
        addSubview(leftArrow)
        addSubview(rightArrow)
        for (k, (heading, rule)) in zip(headings, rules).enumerated() where entries.contains(where: { $0.section == k }) {
            heading.font = .systemFont(ofSize: 17, weight: .bold)
            heading.textColor = NSColor(white: 0.92, alpha: 1)
            rule.boxType = .separator
            let parent: NSView = k < LevelPicker.editions ? strip : self  // the editions': riding with their panes
            parent.addSubview(heading)
            parent.addSubview(rule)
        }
    }

    required init?(coder: NSCoder) { fatalError("not used") }

    var isEmpty: Bool { entries.isEmpty }

    /// Slide to the pane on that side (-1 left, 1 right), if there is one.
    func slide(_ dir: Int) {
        guard panes.indices.contains(pane + dir) else { return }
        pane += dir
        updateArrows()
        NSAnimationContext.runAnimationGroup { context in
            context.duration = 0.25
            context.timingFunction = CAMediaTimingFunction(name: .easeOut)
            strip.animator().setFrameOrigin(stripOrigin)
        }
    }

    private var stripOrigin: NSPoint { NSPoint(x: -CGFloat(pane) * viewport.bounds.width, y: 0) }

    /// Only the arrows with a pane that way.
    private func updateArrows() {
        leftArrow.isHidden = pane == 0
        rightArrow.isHidden = pane >= panes.count - 1
    }

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
        replaysButton.frame = NSRect(x: bx + bw + gap, y: by, width: bw, height: bh)
        /// The tiles from `top` in their parent; the height they take.
        func place(_ members: [Int], top: CGFloat) -> CGFloat {
            for (j, i) in members.enumerated() {
                tiles[i].frame = NSRect(x: left + CGFloat(j % cols) * cellW, y: top + CGFloat(j / cols) * cellH,
                                        width: tile.width, height: tile.height + caption)
            }
            return CGFloat((members.count + cols - 1) / cols) * cellH
        }
        var y = pad + introHeight
        // The panes: two rows high, whatever they hold; their headings centred between the arrows.
        if !panes.isEmpty {
            let paneHeight = 2 * cellH, height = headingHeight + paneHeight
            viewport.frame = NSRect(x: 0, y: y, width: width, height: height)
            strip.frame = NSRect(x: 0, y: 0, width: CGFloat(panes.count) * width, height: height)
            strip.setFrameOrigin(stripOrigin)
            for (j, p) in panes.enumerated() {
                let x = CGFloat(j) * width, heading = headings[p.section]
                heading.sizeToFit()
                heading.frame.origin = NSPoint(x: x + (width - heading.frame.width) / 2, y: 4)
                rules[p.section].frame = NSRect(x: x + left, y: headingHeight - 10, width: rowWidth, height: 1)
                p.scroll.frame = NSRect(x: x, y: headingHeight, width: width, height: paneHeight)
                let rows = place(entries.indices.filter { entries[$0].section == p.section }, top: 0)
                p.grid.setFrameSize(NSSize(width: p.scroll.contentSize.width, height: max(rows, paneHeight)))
            }
            let aw: CGFloat = 56, ah: CGFloat = 32, ay = y + (headingHeight - ah) / 2 - 6
            leftArrow.frame = NSRect(x: left, y: ay, width: aw, height: ah)
            rightArrow.frame = NSRect(x: left + rowWidth - aw, y: ay, width: aw, height: ah)
            y += height
        }
        updateArrows()
        // The rest: each section starts on a new row, under its heading; empty sections take no space.
        for (k, (heading, rule)) in zip(headings, rules).enumerated() where k >= LevelPicker.editions {
            let members = entries.indices.filter { entries[$0].section == k }
            guard !members.isEmpty else { continue }
            heading.sizeToFit()
            heading.frame.origin = NSPoint(x: left, y: y + 4)
            rule.frame = NSRect(x: left, y: y + headingHeight - 10, width: rowWidth, height: 1)
            y += headingHeight
            y += place(members, top: y)
        }
        setFrameSize(NSSize(width: width, height: y))
    }

    @objc private func pick(_ sender: NSButton) { onPick(entries[sender.tag].url) }
    @objc private func intro() { onIntro?() }
    @objc private func replays() { onReplays?() }
    @objc private func slideLeft() { slide(-1) }
    @objc private func slideRight() { slide(1) }
}

/// Lays out from the top, as the level select.
private final class FlippedView: NSView {
    override var isFlipped: Bool { true }
}

/// A pane's scroll view: at its top or bottom the wheel goes on to the page, which scrolls on to the cutscenes.
private final class PaneScrollView: NSScrollView {
    override func scrollWheel(with event: NSEvent) {
        let view = contentView.bounds, dy = event.scrollingDeltaY
        let atEnd = dy > 0 ? view.minY <= 0 : dy < 0 && view.maxY >= (documentView?.frame.height ?? 0) - 1
        if atEnd { nextResponder?.scrollWheel(with: event) } else { super.scrollWheel(with: event) }
    }
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
