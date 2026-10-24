import AppKit

/// The controls as a keyboard: every bound key coloured by what it does, with a short caption, a legend, and the mouse
/// and Cmd shortcuts underneath. Shown in the controls dialog (F1, Cmd-/ or the level select's Controls button); rendered to
/// docs/controls.png for the README by tools/render_controls.sh.
final class ControlsView: NSView {
    enum Kind: CaseIterable {
        case move, camera, ed, fly, mode, view, app

        var title: String {
            switch self {
            case .move: return "Move (relative to the camera)"
            case .camera: return "Camera"
            case .ed: return "Ed: jump, act, shoot, side-step"
            case .fly: return "Free-fly up / down"
            case .mode: return "Play as Ed / free-fly viewer / simple walking"
            case .view: return "View: speed, lighting, gamma, FOV, fog, AA"
            case .app: return "App: music, mouse, help, full screen"
            }
        }

        var color: NSColor {
            switch self {
            case .move: return NSColor(calibratedRed: 0.26, green: 0.52, blue: 0.96, alpha: 1)
            case .camera: return NSColor(calibratedRed: 0.13, green: 0.70, blue: 0.67, alpha: 1)
            case .ed: return NSColor(calibratedRed: 0.97, green: 0.55, blue: 0.16, alpha: 1)
            case .fly: return NSColor(calibratedRed: 0.62, green: 0.40, blue: 0.90, alpha: 1)
            case .mode: return NSColor(calibratedRed: 0.30, green: 0.75, blue: 0.35, alpha: 1)
            case .view: return NSColor(calibratedRed: 0.92, green: 0.78, blue: 0.20, alpha: 1)
            case .app: return NSColor(calibratedRed: 0.62, green: 0.65, blue: 0.72, alpha: 1)
            }
        }
    }

    /// A key: its label, width in key units, and what it does (nil = unbound, drawn plain).
    private struct Key { let label: String; let width: CGFloat; let kind: Kind?; let caption: String }

    private static func k(_ label: String, _ width: CGFloat = 1, _ kind: Kind? = nil, _ caption: String = "") -> Key {
        Key(label: label, width: width, kind: kind, caption: caption)
    }

    /// A US layout, as compact as the bindings allow. Gaps are keys with an empty label and width.
    private static let rows: [[Key]] = [
        [k("Esc", 1, .app, "mouse"), k("", 0.5), k("F1", 1, .app, "help"), k("F2", 1, .view, "AA"), k("F3"), k("F4"), k("", 0.5),
         k("F5"), k("F6"), k("F7"), k("F8"), k("", 0.5), k("F9"), k("F10"), k("F11"), k("F12")],
        [k("`"), k("1", 1, .view, "light 1"), k("2", 1, .view, "light 2"), k("3", 1, .view, "light 3"), k("4", 1, .view),
         k("5", 1, .view), k("6", 1, .view), k("7", 1, .view), k("8", 1, .view), k("9", 1, .view), k("0"),
         k("-", 1, .view, "darker"), k("=", 1, .view, "brighter"), k("Delete", 1.5)],
        [k("Tab", 1.5, .mode, "Ed / fly"), k("Q", 1, .ed, "shoot"), k("W", 1, .move, "forward"), k("E", 1, .ed, "act"),
         k("R", 1, .fly, "up"), k("T"), k("Y"), k("U"), k("I", 1, .mode, "AI mode"), k("O"), k("P", 1, .view, "pause"),
         k("[", 1, .view, "slower"), k("]", 1, .view, "faster"), k("\\")],
        [k("Caps", 1.75), k("A", 1, .move, "left"), k("S", 1, .move, "back"), k("D", 1, .move, "right"), k("F", 1, .fly, "down"),
         k("G", 1, .view, "fog"), k("H"), k("J"), k("K"), k("L"), k(";"), k("'"), k("Return", 1.75)],
        [k("Shift", 2.25, .ed, "walk / fast"), k("Z"), k("X"), k("C", 1, .ed, "side-step"), k("V", 1, .app, "intro*"), k("B"), k("N"),
         k("M", 1, .app, "mute"), k(",", 1, .view, "FOV -"), k(".", 1, .view, "FOV +"), k("/"), k("Shift", 2.25, .ed, "walk / fast")],
        [k("Ctrl", 1.25), k("Opt", 1.25), k("Cmd", 1.5, .app, "see below"), k("Space", 5, .ed, "jump"), k("Cmd", 1.5, .app, "see below"),
         k("Opt", 1.25), k("", 0.4), k("←", 1, .camera, "look"), k("↑", 1, .camera, "look up"), k("↓", 1, .camera, "look dn"),
         k("→", 1, .camera, "look")],
    ]

    private let unit: CGFloat = 54, gap: CGFloat = 6, margin: CGFloat = 18
    private static let notes = [
        "Mouse: click the view to capture it and look around, Esc releases it; or drag. Scroll: camera zoom (as Ed) / fly speed (free-fly).",
        "As Ed with his AI (the default): he runs, Shift walks. Simple controller (I): Shift runs. Portals: walk into them.",
        "Cmd-L level select · Cmd-/ this help · Ctrl-Cmd-F full screen · Cmd-Q quit · 1-9 lighting variants (day / night levels)",
        "* V (or Play Intro Video) plays the intro movie from the level select screen; when it ends, or is skipped, the ski slope starts.",
    ]

    override init(frame: NSRect) {
        super.init(frame: frame)
        setFrameSize(fittingSize)
    }

    required init?(coder: NSCoder) { fatalError("not used") }

    override var isFlipped: Bool { true }

    private var keyboardWidth: CGFloat {
        ControlsView.rows.map { $0.reduce(0) { $0 + $1.width * unit + gap } - gap }.max() ?? 0
    }

    override var fittingSize: NSSize {
        NSSize(width: keyboardWidth + 2 * margin,
               height: margin + CGFloat(ControlsView.rows.count) * (unit + gap) + 14 + 4 * 24 + 8 + CGFloat(ControlsView.notes.count) * 20 + margin)
    }

    override func draw(_ dirtyRect: NSRect) {
        NSColor(calibratedRed: 0.10, green: 0.11, blue: 0.16, alpha: 1).setFill()
        NSBezierPath(roundedRect: bounds, xRadius: 12, yRadius: 12).fill()
        var y = margin
        for row in ControlsView.rows {
            var x = margin
            for key in row {
                let w = key.width * unit
                defer { x += w + gap }
                guard !key.label.isEmpty else { continue }
                drawKey(key, NSRect(x: x, y: y, width: w, height: unit))
            }
            y += unit + gap
        }
        // Legend: two columns of colour swatches.
        y += 14
        let colW = keyboardWidth / 2
        for (i, kind) in Kind.allCases.enumerated() {
            let x = margin + CGFloat(i % 2) * colW, row = y + CGFloat(i / 2) * 24
            kind.color.setFill()
            NSBezierPath(roundedRect: NSRect(x: x, y: row + 3, width: 14, height: 14), xRadius: 3, yRadius: 3).fill()
            text(kind.title, at: NSPoint(x: x + 22, y: row + 1), size: 13, color: .white)
        }
        y += 4 * 24 + 8
        for line in ControlsView.notes {
            text(line, at: NSPoint(x: margin, y: y), size: 12, color: NSColor(calibratedWhite: 0.75, alpha: 1))
            y += 20
        }
    }

    private func drawKey(_ key: Key, _ r: NSRect) {
        let path = NSBezierPath(roundedRect: r, xRadius: 7, yRadius: 7)
        if let kind = key.kind {
            kind.color.withAlphaComponent(0.88).setFill()
        } else {
            NSColor(calibratedWhite: 0.24, alpha: 1).setFill()
        }
        path.fill()
        NSColor(calibratedWhite: 0, alpha: 0.35).setStroke()
        path.lineWidth = 1
        path.stroke()
        let ink: NSColor = key.kind == nil ? NSColor(calibratedWhite: 0.55, alpha: 1)
                                           : NSColor(calibratedWhite: 0.07, alpha: 1)
        let big = key.label.count <= 2
        text(key.label, at: NSPoint(x: r.minX + 7, y: r.minY + 5), size: big ? 17 : 12,
             color: ink, weight: .semibold, width: r.width - 10)
        if !key.caption.isEmpty {
            text(key.caption, at: NSPoint(x: r.minX + 6, y: r.maxY - 17), size: 10, color: ink, width: r.width - 8)
        }
    }

    private func text(_ s: String, at p: NSPoint, size: CGFloat, color: NSColor, weight: NSFont.Weight = .regular,
                      width: CGFloat? = nil) {
        let style = NSMutableParagraphStyle()
        style.lineBreakMode = .byTruncatingTail
        let attrs: [NSAttributedString.Key: Any] = [.font: NSFont.systemFont(ofSize: size, weight: weight),
                                                    .foregroundColor: color, .paragraphStyle: style]
        let str = NSAttributedString(string: s, attributes: attrs)
        if let width {
            str.draw(with: NSRect(x: p.x, y: p.y, width: width, height: size * 1.4), options: [.usesLineFragmentOrigin, .truncatesLastVisibleLine])
        } else {
            str.draw(at: p)
        }
    }

    /// The view as a PNG (for the README).
    func pngData(scale: CGFloat = 2) -> Data? {
        let size = fittingSize
        guard let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: Int(size.width * scale), pixelsHigh: Int(size.height * scale),
                                         bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
                                         colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0) else { return nil }
        rep.size = size
        guard let bitmap = NSGraphicsContext(bitmapImageRep: rep) else { return nil }
        NSGraphicsContext.saveGraphicsState()
        // The view draws top-down (isFlipped): flip the bitmap's coordinates and say so, so text is upright.
        let cg = bitmap.cgContext
        cg.translateBy(x: 0, y: size.height)
        cg.scaleBy(x: 1, y: -1)
        NSGraphicsContext.current = NSGraphicsContext(cgContext: cg, flipped: true)
        draw(NSRect(origin: .zero, size: size))
        NSGraphicsContext.restoreGraphicsState()
        return rep.representation(using: .png, properties: [:])
    }
}
