import AppKit
import TonicCore

/// The Replays menu: the recordings (Recording) in a list, newest first, each to play or delete; sdl/src/replays.c is the
/// SDL build's. Row text is the core's (tc_replay_summarize). Deleting asks first; an autopilot run can't be played. A run
/// ends on the way to a menu, so nothing here is being written. Up / Down select, Return plays, Delete deletes, R flips
/// Record runs (Recording.enabled), Esc goes back.
final class ReplaysScreen: NSView {
    private struct Entry { let url: URL; let title: String; let details: String; let playable: Bool }

    /// The rows' view, top down.
    private final class List: NSView { override var isFlipped: Bool { true } }

    var onBack: (() -> Void)?
    var onPlay: ((URL) -> Void)?
    private var entries: [Entry]
    private var rows: [NSBox] = []
    private var selected = -1
    private let list = List()
    private let scroll = NSScrollView()
    private let empty: NSTextField
    private let record = NSButton(checkboxWithTitle: "Record runs", target: nil, action: nil)
    private let rowHeight: CGFloat = 72, gap: CGFloat = 8, header: CGFloat = 64

    override var acceptsFirstResponder: Bool { true }

    init(frame: NSRect, folder: URL?) {
        let files = folder.flatMap { try? FileManager.default.contentsOfDirectory(at: $0, includingPropertiesForKeys: nil) } ?? []
        entries = files.filter { $0.pathExtension == TC_REC_EXTENSION }
            .sorted { $0.lastPathComponent > $1.lastPathComponent }  // the names start with their date
            .map { Self.read($0) }
        empty = NSTextField(labelWithString: folder == nil ? "Recording is off (TONIC_RECORDINGS)."
                                                           : "No recordings yet: turn on Record runs, then play a level.")
        super.init(frame: frame)
        appearance = NSAppearance(named: .darkAqua)  // a dark screen in light mode too (readable text)
        autoresizingMask = [.width, .height]
        wantsLayer = true
        layer?.backgroundColor = NSColor(calibratedRed: 0.08, green: 0.09, blue: 0.14, alpha: 1).cgColor

        let title = NSTextField(labelWithString: "Replays")
        title.font = .systemFont(ofSize: 20, weight: .bold)
        title.sizeToFit()
        title.frame.origin = NSPoint(x: (bounds.width - title.frame.width) / 2, y: bounds.height - header + (header - title.frame.height) / 2)
        title.autoresizingMask = [.minXMargin, .maxXMargin, .minYMargin]
        addSubview(title)
        let back = NSButton(title: "Back", target: self, action: #selector(goBack))
        back.bezelStyle = .rounded
        back.toolTip = "The level select (Esc)"
        back.sizeToFit()
        back.frame.origin = NSPoint(x: 16, y: bounds.height - header + (header - back.frame.height) / 2)
        back.autoresizingMask = [.maxXMargin, .minYMargin]
        addSubview(back)
        record.target = self
        record.action = #selector(toggleRecord)
        record.state = Recording.enabled ? .on : .off
        record.isEnabled = folder != nil
        record.toolTip = "Save each run from here on, to play back in this list (R). Off by default: the files add up."
        record.sizeToFit()
        record.frame.origin = NSPoint(x: bounds.width - record.frame.width - 16, y: bounds.height - header + (header - record.frame.height) / 2)
        record.autoresizingMask = [.minXMargin, .minYMargin]
        addSubview(record)

        scroll.frame = NSRect(x: 0, y: 0, width: bounds.width, height: bounds.height - header)
        scroll.autoresizingMask = [.width, .height]
        scroll.hasVerticalScroller = true
        scroll.drawsBackground = false
        scroll.documentView = list
        addSubview(scroll)
        empty.textColor = NSColor(white: 0.7, alpha: 1)
        empty.sizeToFit()
        empty.frame.origin = NSPoint(x: (bounds.width - empty.frame.width) / 2, y: bounds.height / 2)
        empty.autoresizingMask = [.minXMargin, .maxXMargin, .minYMargin, .maxYMargin]
        addSubview(empty)
        rebuild()
    }

    required init?(coder: NSCoder) { fatalError("not used") }

    /// A row's text and state from the file.
    private static func read(_ url: URL) -> Entry {
        guard let data = try? Data(contentsOf: url) else { return unreadable(url) }
        return data.withUnsafeBytes { bytes -> Entry in
            var r = tc_replay()
            guard tc_replay_open(&r, bytes.baseAddress, bytes.count) == TC_OK else { return unreadable(url) }
            var s = tc_replay_summary()
            tc_replay_summarize(&r, &s)
            tc_replay_close(&r)
            let when = cString(s.when)
            let details = (when.isEmpty ? "" : when + ", ") + cString(s.details)
            return Entry(url: url, title: cString(s.levels), details: details, playable: s.playable)
        }
    }

    private static func unreadable(_ url: URL) -> Entry {
        Entry(url: url, title: url.deletingPathExtension().lastPathComponent, details: "not a recording this build can read",
              playable: false)
    }

    /// The rows anew, after opening or a delete.
    private func rebuild() {
        rows.forEach { $0.removeFromSuperview() }
        rows = []
        empty.isHidden = !entries.isEmpty
        let width = min(scroll.contentSize.width - 40, 1100), left = (scroll.contentSize.width - width) / 2
        for (i, e) in entries.enumerated() {
            let row = NSBox(frame: NSRect(x: left, y: CGFloat(i) * (rowHeight + gap), width: width, height: rowHeight))
            row.boxType = .custom
            row.borderWidth = 0
            row.cornerRadius = 6
            row.contentViewMargins = .zero
            let inner = row.contentView ?? row
            let title = NSTextField(labelWithString: e.title), details = NSTextField(labelWithString: e.details)
            title.font = .systemFont(ofSize: 15, weight: .semibold)
            details.font = .systemFont(ofSize: 12)
            details.textColor = NSColor(white: 0.68, alpha: 1)
            for label in [title, details] {
                label.lineBreakMode = .byTruncatingTail
                label.autoresizingMask = [.width]
                inner.addSubview(label)
            }
            let delete = NSButton(title: "Delete", target: self, action: #selector(deleteRow(_:)))
            let play = NSButton(title: "Play", target: self, action: #selector(playRow(_:)))
            var x = width - 14
            for (button, enabled) in [(delete, true), (play, e.playable)] {
                button.bezelStyle = .rounded
                button.tag = i
                button.isEnabled = enabled
                button.sizeToFit()
                button.frame.size.width = max(button.frame.width, 84)
                x -= button.frame.width
                button.frame.origin = NSPoint(x: x, y: (rowHeight - button.frame.height) / 2)
                button.autoresizingMask = [.minXMargin]
                x -= 8
                inner.addSubview(button)
            }
            title.frame = NSRect(x: 16, y: rowHeight - 34, width: x - 24, height: 20)
            details.frame = NSRect(x: 16, y: 12, width: x - 24, height: 16)
            list.addSubview(row)
            rows.append(row)
        }
        list.frame = NSRect(x: 0, y: 0, width: scroll.contentSize.width, height: CGFloat(entries.count) * (rowHeight + gap))
        select(min(selected, entries.count - 1))
    }

    private func select(_ i: Int) {
        selected = i
        for (k, row) in rows.enumerated() {
            row.fillColor = k == i ? NSColor(calibratedRed: 0.2, green: 0.23, blue: 0.34, alpha: 1)
                                   : NSColor(calibratedRed: 0.14, green: 0.15, blue: 0.22, alpha: 1)
        }
        if i >= 0, i < rows.count { rows[i].scrollToVisible(rows[i].bounds) }
    }

    /// Asks, then removes the file and its row.
    private func delete(_ i: Int) {
        guard entries.indices.contains(i) else { return }
        let e = entries[i], alert = NSAlert()
        alert.alertStyle = .warning
        alert.messageText = "Delete this recording for good?"
        alert.informativeText = "\(e.title)\n\(e.details)"
        alert.addButton(withTitle: "Delete").hasDestructiveAction = true
        alert.addButton(withTitle: "Keep")
        guard alert.runModal() == .alertFirstButtonReturn else { return }
        do {
            try FileManager.default.removeItem(at: e.url)
            entries.remove(at: i)
            rebuild()
        } catch {
            let failed = NSAlert()
            failed.messageText = "That recording could not be deleted"
            failed.informativeText = "\(error.localizedDescription)"
            failed.runModal()
        }
    }

    private func play(_ i: Int) {
        if entries.indices.contains(i), entries[i].playable { onPlay?(entries[i].url) }
    }

    @objc private func goBack() { onBack?() }
    @objc private func toggleRecord() { Recording.enabled = record.state == .on }
    @objc private func playRow(_ sender: NSButton) { play(sender.tag) }
    @objc private func deleteRow(_ sender: NSButton) { delete(sender.tag) }

    override func keyDown(with event: NSEvent) {
        switch event.keyCode {
        case 53: if !event.isARepeat { onBack?() }  // Esc
        case 126: if !entries.isEmpty { select(max(selected - 1, 0)) }  // up
        case 125: if !entries.isEmpty { select(min(selected + 1, entries.count - 1)) }  // down
        case 36, 76: if !event.isARepeat { play(selected) }  // Return, keypad Enter
        case 51, 117: if !event.isARepeat { delete(selected) }  // Delete, forward delete
        case 15: if !event.isARepeat, record.isEnabled { record.performClick(nil) }  // R
        default: super.keyDown(with: event)
        }
    }
}
