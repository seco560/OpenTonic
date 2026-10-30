import AVFoundation
import AppKit

/// The intro movie (V on the level select screen): files/out/video/intro.mp4, built from the disc's EDPROL.AVI
/// (400x240 Indeo, no sound) upscaled to 1800x1080 and muxed with its soundtrack, the game's sound bank 200
/// (English/BNK_200.BNM -> USDOLBY.APM), which the exe fires as event 0 when it starts the video (docs/BUILD.md,
/// docs/NOTES.md "Intro video"). The picture keeps its 5:3 shape at `screenFraction` of the largest size that fits,
/// centred on black (larger than the original's small window, but it is 240p underneath).
/// Esc, Space or Return skips; at the end (or on skip) onDone runs.
final class IntroView: NSView {
    private let player: AVPlayer
    private let video: AVPlayerLayer
    private let aspect: CGFloat = 400.0 / 240.0  // EDPROL.AVI
    var screenFraction: CGFloat = 0.8
    private let onDone: () -> Void
    private var finished = false

    init(frame: NSRect, url: URL, onDone: @escaping () -> Void) {
        player = AVPlayer(url: url)
        video = AVPlayerLayer(player: player)
        self.onDone = onDone
        super.init(frame: frame)
        wantsLayer = true
        layer?.backgroundColor = NSColor.black.cgColor
        video.videoGravity = .resizeAspect
        layer?.addSublayer(video)
        layoutVideo()
        NotificationCenter.default.addObserver(self, selector: #selector(ended), name: .AVPlayerItemDidPlayToEndTime,
                                               object: player.currentItem)  // removed automatically on dealloc
    }

    required init?(coder: NSCoder) { fatalError("not used") }

    func start() { player.play() }

    override func layout() {
        super.layout()
        layoutVideo()
    }

    /// Largest 5:3 rectangle that fits, scaled by screenFraction, centred.
    private func layoutVideo() {
        var w = bounds.width, h = w / aspect
        if h > bounds.height { h = bounds.height; w = h * aspect }
        w *= screenFraction
        h *= screenFraction
        CATransaction.begin()
        CATransaction.setDisableActions(true)  // no implicit animation while the window resizes
        video.frame = CGRect(x: (bounds.width - w) / 2, y: (bounds.height - h) / 2, width: w, height: h)
        CATransaction.commit()
    }

    override var acceptsFirstResponder: Bool { true }

    override func keyDown(with event: NSEvent) {
        switch event.keyCode {
        case 53, 49, 36, 76: finish()  // Esc, Space, Return, keypad Enter
        default: super.keyDown(with: event)
        }
    }

    @objc private func ended() { finish() }

    private func finish() {
        guard !finished else { return }
        finished = true
        player.pause()
        onDone()
    }
}

/// The level select screen's root view: V plays the intro, D starts the demo.
final class PickerContainer: NSView {
    var onIntro: (() -> Void)?
    var onDemo: (() -> Void)?

    override var acceptsFirstResponder: Bool { true }

    override func keyDown(with event: NSEvent) {
        let plain = event.modifierFlags.intersection([.command, .control, .option]).isEmpty
        switch event.charactersIgnoringModifiers?.lowercased() {
        case "v" where plain: onIntro?()
        case "d" where plain: onDemo?()
        default: super.keyDown(with: event)
        }
    }
}
