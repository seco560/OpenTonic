import AppKit
import QuartzCore
import TonicCore

/// Grid of converted levels (*.ttlv in the levels folder, and in the Special Edition's out/se/ttlv), each shown with
/// its loading-screen image from out/Vignette/Level/<Name>.png (the Special Edition's: out/se/Vignette/Level) when
/// available (matched case-insensitively).
/// Four sections, four panes in a ring, one showing at a time: "Retail Edition", "Special Edition" (the 1998 disc's
/// levels, tools/ttre/se.py), "Cutscenes" (the retail game's cutscene-only levels, `tc_level_is_cutscene`) and "Imported"
/// (models brought in with tools/ttre/import_gltf.py: they have a folder out/Imported/<name>, and a question mark as
/// their picture). Each scrolls inside itself; the arrows at their top slide left and right, in that order and round
/// again. The page itself only scrolls in a window too short for a row.
final class LevelPicker: NSView {
    struct Entry { let name: String; let url: URL; let image: NSImage?; let section: Int }
    /// A pane: its page (heading, rule and scroll view, sliding as one) in the viewport.
    private struct Pane { let section: Int; let page: NSView; let scroll: NSScrollView; let grid: NSView }

    private static let titles = ["Retail Edition", "Special Edition", "Cutscenes", "Imported"]
    private static let specialEdition = 1, cutscenes = 2, imported = 3
    private static let paneSections = 4  // all the sections are panes, Imported the last
    private let entries: [Entry]
    private let onPick: (URL) -> Void
    /// The big buttons at the top: "Play Intro Video" (the movie then starts the ski slope, as the original), "Random Stage"
    /// (pickRandom) and "Replays" (the recordings: ReplaysScreen).
    var onIntro: (() -> Void)?
    var onReplays: (() -> Void)?
    private let introButton = PickerButton(title: "▶  Play Intro Video", colors: (NSColor(calibratedRed: 0.95, green: 0.45, blue: 0.2, alpha: 1),
                                                                         NSColor(calibratedRed: 0.8, green: 0.15, blue: 0.45, alpha: 1)))
    private let randomButton = PickerButton(title: "⚄  Random Stage", colors: (NSColor(calibratedRed: 0.45, green: 0.8, blue: 0.3, alpha: 1),
                                                                   NSColor(calibratedRed: 0.1, green: 0.55, blue: 0.4, alpha: 1)))
    private let replaysButton = PickerButton(title: "✦  Replays", colors: (NSColor(calibratedRed: 0.2, green: 0.75, blue: 0.85, alpha: 1),
                                                               NSColor(calibratedRed: 0.25, green: 0.3, blue: 0.85, alpha: 1)))
    private let leftArrow = PickerButton(title: "◀", colors: LevelPicker.arrowColors)
    private let rightArrow = PickerButton(title: "▶", colors: LevelPicker.arrowColors)
    private static let arrowColors = (NSColor(calibratedRed: 0.34, green: 0.38, blue: 0.56, alpha: 1),
                                      NSColor(calibratedRed: 0.22, green: 0.25, blue: 0.4, alpha: 1))
    private let introHeight: CGFloat = 84
    private var tiles: [NSButton] = []
    /// The window onto the panes (the full width, so a pane leaves by the window's side): the one showing fills it, the
    /// others wait out of sight beside it.
    private let viewport = FlippedView()
    private var panes: [Pane] = []
    private var pane = 0  // the one showing, in `panes`
    /// Where the ring of panes stands, in panes (panes.count is the first again), and where it eases to: one step per
    /// slide whichever way round, as the SDL picker. Each display refresh places the pages from it (placePages). Not
    /// the animator: a page moved to the arrow's side and animated in the same pass started from where its layer last
    /// was (AppKit updates it later), so the wrap from Cutscenes to Retail and back swept the wrong way.
    private var slidePos: CGFloat = 0, slideTo: CGFloat = 0
    private var slideLink: CADisplayLink?
    private var slideTime: CFTimeInterval = 0
    /// Under the panes: the imported levels, heading and all.
    private let lower = PaneScrollView(), lowerGrid = FlippedView()
    private let headings = LevelPicker.titles.map { NSTextField(labelWithString: $0) }
    private let rules = LevelPicker.titles.map { _ in NSBox() }
    private let headingHeight: CGFloat = 40
    private let tile = NSSize(width: 256, height: 192)
    private let caption: CGFloat = 26, pad: CGFloat = 20

    override var isFlipped: Bool { true }

    /// showSection: the section whose pane shows first (when it has levels; else the first that has).
    init(folder: URL, thumbnails: URL, seFolder: URL? = nil, seThumbnails: URL? = nil, showSection: Int = 0,
         onPick: @escaping (URL) -> Void) {
        self.onPick = onPick
        let fm = FileManager.default
        let imported = folder.deletingLastPathComponent().appendingPathComponent("Imported")
        func section(_ url: URL) -> Int {
            let name = url.deletingPathExtension().lastPathComponent
            if fm.fileExists(atPath: imported.appendingPathComponent(name).path) { return LevelPicker.imported }
            return tc_level_is_cutscene(name) != 0 ? LevelPicker.cutscenes : 0
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
        addSubview(viewport)
        for k in 0..<LevelPicker.paneSections where entries.contains(where: { $0.section == k }) {
            let page = FlippedView(), scroll = PaneScrollView(), grid = FlippedView()
            scroll.hold(grid)
            page.addSubview(scroll)
            viewport.addSubview(page)
            panes.append(Pane(section: k, page: page, scroll: scroll, grid: grid))
        }
        lower.hold(lowerGrid)
        addSubview(lower)
        pane = panes.firstIndex { $0.section == showSection } ?? 0
        slidePos = CGFloat(pane)
        slideTo = slidePos
        for (i, e) in entries.enumerated() {
            let b = NSButton(title: e.name, image: e.image ?? NSImage(size: tile), target: self, action: #selector(pick(_:)))
            b.tag = i
            b.imagePosition = .imageAbove
            b.imageScaling = .scaleProportionallyUpOrDown
            b.isBordered = false
            b.attributedTitle = NSAttributedString(string: e.name, attributes: [
                .font: NSFont.systemFont(ofSize: 15, weight: .semibold), .foregroundColor: NSColor.white, .shadow: shadow])
            b.toolTip = e.url.path
            let parent: NSView = panes.first { $0.section == e.section }?.grid ?? lowerGrid
            parent.addSubview(b)
            tiles.append(b)
        }
        introButton.target = self
        introButton.action = #selector(intro)
        introButton.toolTip = "The game's intro movie (V)"
        randomButton.target = self
        randomButton.action = #selector(random)
        randomButton.toolTip = "A stage of either edition, at random"
        replaysButton.target = self
        replaysButton.action = #selector(replays)
        replaysButton.toolTip = "Your recorded sessions: play or delete them"
        leftArrow.target = self
        leftArrow.action = #selector(slideLeft)
        rightArrow.target = self
        rightArrow.action = #selector(slideRight)
        addSubview(introButton)
        addSubview(randomButton)
        addSubview(replaysButton)
        addSubview(leftArrow)
        addSubview(rightArrow)
        for (k, (heading, rule)) in zip(headings, rules).enumerated() where entries.contains(where: { $0.section == k }) {
            heading.font = .systemFont(ofSize: 17, weight: .bold)
            heading.textColor = NSColor(white: 0.92, alpha: 1)
            rule.boxType = .separator
            let parent: NSView = panes.first { $0.section == k }?.page ?? lowerGrid  // the panes': riding with them
            parent.addSubview(heading)
            parent.addSubview(rule)
        }
    }

    required init?(coder: NSCoder) { fatalError("not used") }

    var isEmpty: Bool { entries.isEmpty }

    /// Opens a stage of either edition at random. Not the cutscenes (the Special Edition's pane has one too: its
    /// outro), nor the imported models.
    func pickRandom() {
        let stages = entries.filter { $0.section < LevelPicker.cutscenes && tc_level_is_cutscene($0.name) == 0 }
        if let stage = stages.randomElement() { onPick(stage.url) }
    }

    /// Slide to the pane on that side (-1 left, 1 right), round the ring: left of retail are the cutscenes. The pane
    /// slid to comes in from that side whichever it is, the one showing leaving by the other.
    func slide(_ dir: Int) {
        guard panes.count > 1, dir != 0 else { return }
        let dir = dir < 0 ? -1 : 1
        pane = (pane + dir + panes.count) % panes.count
        slideTo += CGFloat(dir)
        updateArrows()
        guard slideLink == nil else { return }  // already easing: it heads for the new place
        let link = displayLink(target: self, selector: #selector(stepSlide(_:)))
        link.add(to: .main, forMode: .common)
        slideLink = link
        slideTime = 0
    }

    /// One display refresh of the slide: eased by the time since the last (SDL's rate), then the pages placed; once
    /// there, both back into [0, n) (the same place in the ring) and the link stops.
    @objc private func stepSlide(_ link: CADisplayLink) {
        let dt = slideTime > 0 ? link.timestamp - slideTime : 1.0 / 60
        slideTime = link.timestamp
        slidePos += (slideTo - slidePos) * CGFloat(1 - exp(-14 * dt))
        if abs(slideTo - slidePos) < 0.001 { settleSlide() }
        placePages()
    }

    private func settleSlide() {
        let n = CGFloat(max(1, panes.count))
        slideTo -= (slideTo / n).rounded(.down) * n
        slidePos = slideTo
        slideLink?.invalidate()
        slideLink = nil
    }

    /// Off the screen (a level picked mid-slide): no more refreshes; it would be back where it was going.
    override func viewDidMoveToWindow() {
        super.viewDidMoveToWindow()
        if window == nil, slideLink != nil { settleSlide() }
    }

    /// Each page where the slide has it: side by side, the nearer way round the ring (so the pane slid to comes in
    /// from the side of the arrow); those wholly out of sight hidden.
    private func placePages() {
        let n = CGFloat(max(1, panes.count)), width = viewport.bounds.width
        for (j, p) in panes.enumerated() {
            var d = CGFloat(j) - slidePos
            d -= n * ((d + n / 2) / n).rounded(.down)
            p.page.setFrameOrigin(NSPoint(x: (d * width).rounded(), y: 0))
            p.page.isHidden = abs(d) >= 1
        }
    }

    /// The arrows go round the ring (with two panes or more); their tips name the pane each leads to.
    private func updateArrows() {
        let round = panes.count > 1
        leftArrow.isHidden = !round
        rightArrow.isHidden = !round
        guard round else { return }
        func title(_ dir: Int) -> String { LevelPicker.titles[panes[(pane + dir + panes.count) % panes.count].section] }
        leftArrow.toolTip = "\(title(-1)) (left arrow key)"
        rightArrow.toolTip = "\(title(1)) (right arrow key)"
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

    /// Lay the tiles out for the given window size and size self to fit (we live in a scroll view).
    func relayout(size: NSSize) {
        let width = size.width
        let cellW = tile.width + pad, cellH = tile.height + caption + pad
        let cols = max(1, Int((width - pad) / cellW))
        let left = max(pad, (width - CGFloat(cols) * cellW + pad) / 2)
        let rowWidth = CGFloat(cols) * cellW - pad
        // Centred, clear of the Controls button in the corner on both sides.
        let bh: CGFloat = 54, gap: CGFloat = 24, controls: CGFloat = 120
        let bw = min(280, (width - 2 * controls - 2 * gap) / 3)
        let bx = (width - 3 * bw - 2 * gap) / 2, by = pad + (introHeight - bh) / 2 - 8
        for (i, button) in [introButton, randomButton, replaysButton].enumerated() {
            button.frame = NSRect(x: bx + CGFloat(i) * (bw + gap), y: by, width: bw, height: bh)
        }
        /// The tiles from `top` in their parent; the height they take.
        func place(_ members: [Int], top: CGFloat) -> CGFloat {
            for (j, i) in members.enumerated() {
                tiles[i].frame = NSRect(x: left + CGFloat(j % cols) * cellW, y: top + CGFloat(j / cols) * cellH,
                                        width: tile.width, height: tile.height + caption)
            }
            return CGFloat((members.count + cols - 1) / cols) * cellH
        }
        // The rest: each section starts on a new row, under its heading; empty sections take no space.
        var lowerRows: CGFloat = 0
        for (k, (heading, rule)) in zip(headings, rules).enumerated() where k >= LevelPicker.paneSections {
            let members = entries.indices.filter { entries[$0].section == k }
            guard !members.isEmpty else { continue }
            heading.sizeToFit()
            heading.frame.origin = NSPoint(x: left, y: lowerRows + 4)
            rule.frame = NSRect(x: left, y: lowerRows + headingHeight - 10, width: rowWidth, height: 1)
            lowerRows += headingHeight
            lowerRows += place(members, top: lowerRows)
        }
        // The window's bottom third for them, or a row if that's more, or just what they fill; the panes get what's left.
        let top = pad + introHeight, oneRow = headingHeight + cellH
        let lowerHeight = panes.isEmpty ? max(size.height - top, oneRow)
            : min(lowerRows, max((size.height / 3).rounded(.down), oneRow))
        var y = top
        // The panes, where the slide has them (placePages); their headings centred between the arrows.
        if !panes.isEmpty {
            let height = max(size.height - top - lowerHeight, oneRow), paneHeight = height - headingHeight
            viewport.frame = NSRect(x: 0, y: y, width: width, height: height)
            for p in panes {
                let heading = headings[p.section]
                p.page.setFrameSize(NSSize(width: width, height: height))
                heading.sizeToFit()
                heading.frame.origin = NSPoint(x: (width - heading.frame.width) / 2, y: 4)
                rules[p.section].frame = NSRect(x: left, y: headingHeight - 10, width: rowWidth, height: 1)
                p.scroll.frame = NSRect(x: 0, y: headingHeight, width: width, height: paneHeight)
                let rows = place(entries.indices.filter { entries[$0].section == p.section }, top: 0)
                p.grid.setFrameSize(NSSize(width: p.scroll.contentSize.width, height: max(rows, paneHeight)))
            }
            placePages()
            let aw: CGFloat = 56, ah: CGFloat = 32, ay = y + (headingHeight - ah) / 2 - 6
            leftArrow.frame = NSRect(x: left, y: ay, width: aw, height: ah)
            rightArrow.frame = NSRect(x: left + rowWidth - aw, y: ay, width: aw, height: ah)
            y += height
        }
        updateArrows()
        lower.frame = NSRect(x: 0, y: y, width: width, height: lowerHeight)
        lowerGrid.setFrameSize(NSSize(width: lower.contentSize.width, height: max(lowerRows, lowerHeight)))
        setFrameSize(NSSize(width: width, height: y + lowerHeight))
    }

    @objc private func pick(_ sender: NSButton) { onPick(entries[sender.tag].url) }
    @objc private func intro() { onIntro?() }
    @objc private func random() { pickRandom() }
    @objc private func replays() { onReplays?() }
    @objc private func slideLeft() { slide(-1) }
    @objc private func slideRight() { slide(1) }
}

/// Lays out from the top, as the level select.
private final class FlippedView: NSView {
    override var isFlipped: Bool { true }
}

/// A pane's scroll view: at its top or bottom the wheel goes on to the page (which scrolls if the window is too short).
private final class PaneScrollView: NSScrollView {
    /// Set up to scroll `grid`, on the screen's own background.
    func hold(_ grid: NSView) {
        hasVerticalScroller = true
        autohidesScrollers = true
        drawsBackground = false
        automaticallyAdjustsContentInsets = false
        verticalScrollElasticity = .none
        documentView = grid
    }

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
        func label(_ points: CGFloat) -> NSAttributedString {
            NSAttributedString(string: title, attributes: [
                .font: NSFont.systemFont(ofSize: points, weight: .bold), .foregroundColor: NSColor.white, .shadow: shadow])
        }
        var text = label(20)
        let room = r.width - 16
        if text.size().width > room { text = label(20 * room / text.size().width) }  // a narrow window
        let size = text.size()
        text.draw(at: NSPoint(x: r.midX - size.width / 2, y: r.midY - size.height / 2))
    }
}
