import AppKit

/// Grid of converted levels (*.ttlv in one folder), each shown with its loading-screen image
/// from out/Vignette/Level/<Name>.png when available (matched case-insensitively).
final class LevelPicker: NSView {
    struct Entry { let name: String; let url: URL; let image: NSImage? }

    private let entries: [Entry]
    private let onPick: (URL) -> Void
    private var tiles: [NSButton] = []
    private let tile = NSSize(width: 256, height: 192)
    private let caption: CGFloat = 26, pad: CGFloat = 20

    override var isFlipped: Bool { true }

    init(folder: URL, thumbnails: URL, onPick: @escaping (URL) -> Void) {
        self.onPick = onPick
        let fm = FileManager.default
        let levels = ((try? fm.contentsOfDirectory(at: folder, includingPropertiesForKeys: nil)) ?? [])
            .filter { $0.pathExtension.lowercased() == "ttlv" }
            .sorted { $0.lastPathComponent.localizedCaseInsensitiveCompare($1.lastPathComponent) == .orderedAscending }
        let thumbs = Dictionary(((try? fm.contentsOfDirectory(at: thumbnails, includingPropertiesForKeys: nil)) ?? [])
            .filter { $0.pathExtension.lowercased() == "png" }
            .map { ($0.deletingPathExtension().lastPathComponent.lowercased(), $0) }, uniquingKeysWith: { a, _ in a })
        entries = levels.map { url in
            let name = url.deletingPathExtension().lastPathComponent
            return Entry(name: name, url: url, image: thumbs[name.lowercased()].flatMap { NSImage(contentsOf: $0) })
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
    }

    required init?(coder: NSCoder) { fatalError("not used") }

    var isEmpty: Bool { entries.isEmpty }

    /// Lay the tiles out for the given width and size self to fit (we live in a scroll view).
    func relayout(width: CGFloat) {
        let cellW = tile.width + pad, cellH = tile.height + caption + pad
        let cols = max(1, Int((width - pad) / cellW))
        let left = max(pad, (width - CGFloat(cols) * cellW + pad) / 2)
        for (i, b) in tiles.enumerated() {
            b.frame = NSRect(x: left + CGFloat(i % cols) * cellW, y: pad + CGFloat(i / cols) * cellH,
                             width: tile.width, height: tile.height + caption)
        }
        let rows = (tiles.count + cols - 1) / cols
        setFrameSize(NSSize(width: width, height: pad + CGFloat(rows) * cellH))
    }

    @objc private func pick(_ sender: NSButton) { onPick(entries[sender.tag].url) }
}
