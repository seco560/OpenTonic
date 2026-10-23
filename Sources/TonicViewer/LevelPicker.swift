import AppKit
import TonicCore

/// Grid of converted levels (*.ttlv in one folder), each shown with its loading-screen image
/// from out/Vignette/Level/<Name>.png when available (matched case-insensitively).
/// Two sections under headings: "Levels", then the game's cutscene-only levels (`tc_level_is_cutscene`).
final class LevelPicker: NSView {
    struct Entry { let name: String; let url: URL; let image: NSImage? }

    private let entries: [Entry]
    private let onPick: (URL) -> Void
    private var tiles: [NSButton] = []
    private let firstCutscene: Int  // entries[firstCutscene...] are cutscene levels
    private let headings = [NSTextField(labelWithString: "Levels"), NSTextField(labelWithString: "Cutscenes")]
    private let rules = [NSBox(), NSBox()]
    private let headingHeight: CGFloat = 40
    private let tile = NSSize(width: 256, height: 192)
    private let caption: CGFloat = 26, pad: CGFloat = 20

    override var isFlipped: Bool { true }

    init(folder: URL, thumbnails: URL, onPick: @escaping (URL) -> Void) {
        self.onPick = onPick
        let fm = FileManager.default
        func isCutscene(_ url: URL) -> Bool { tc_level_is_cutscene(url.deletingPathExtension().lastPathComponent) != 0 }
        let levels = ((try? fm.contentsOfDirectory(at: folder, includingPropertiesForKeys: nil)) ?? [])
            .filter { $0.pathExtension.lowercased() == "ttlv" }
            .sorted {  // playable levels first, then the cutscene levels; by name within each
                isCutscene($0) != isCutscene($1) ? !isCutscene($0)
                    : $0.lastPathComponent.localizedCaseInsensitiveCompare($1.lastPathComponent) == .orderedAscending
            }
        firstCutscene = levels.firstIndex(where: isCutscene) ?? levels.count
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
        for (k, (heading, rule)) in zip(headings, rules).enumerated() {
            heading.font = .systemFont(ofSize: 15, weight: .semibold)
            heading.textColor = .secondaryLabelColor
            rule.boxType = .separator
            if k == 0 ? firstCutscene > 0 : firstCutscene < entries.count { addSubview(heading); addSubview(rule) }
        }
    }

    required init?(coder: NSCoder) { fatalError("not used") }

    var isEmpty: Bool { entries.isEmpty }

    /// Lay the tiles out for the given width and size self to fit (we live in a scroll view).
    func relayout(width: CGFloat) {
        let cellW = tile.width + pad, cellH = tile.height + caption + pad
        let cols = max(1, Int((width - pad) / cellW))
        let left = max(pad, (width - CGFloat(cols) * cellW + pad) / 2)
        func rows(_ n: Int) -> Int { (n + cols - 1) / cols }
        // Each section starts on a new row, under its heading.
        let headingY = [pad / 2, pad / 2 + headingHeight + CGFloat(rows(firstCutscene)) * cellH]
        for (i, b) in tiles.enumerated() {
            let cut = i >= firstCutscene, j = cut ? i - firstCutscene : i
            let top = headingY[cut ? 1 : 0] + headingHeight
            b.frame = NSRect(x: left + CGFloat(j % cols) * cellW, y: top + CGFloat(j / cols) * cellH,
                             width: tile.width, height: tile.height + caption)
        }
        let rowWidth = CGFloat(cols) * cellW - pad
        for (k, (heading, rule)) in zip(headings, rules).enumerated() {
            heading.sizeToFit()
            heading.frame.origin = NSPoint(x: left, y: headingY[k] + 4)
            rule.frame = NSRect(x: left, y: headingY[k] + headingHeight - 10, width: rowWidth, height: 1)
        }
        let end = headingY[1] + (firstCutscene < tiles.count ? headingHeight + CGFloat(rows(tiles.count - firstCutscene)) * cellH : 0)
        setFrameSize(NSSize(width: width, height: end))
    }

    @objc private func pick(_ sender: NSButton) { onPick(entries[sender.tag].url) }
}
