#!/bin/sh
# Renders the controls picture (Sources/OpenTonic/ControlsView.swift, the app's controls dialog) to docs/controls.png
# for the README: compiles that one file with a three-line main into a throwaway binary. macOS only.
# Usage (from the repo root): tools/render_controls.sh [out.png]
set -e
OUT="${1:-docs/controls.png}"
TMP="$(mktemp -d)"
cat > "$TMP/main.swift" <<'SWIFT'
import AppKit
let png = ControlsView(frame: .zero).pngData(scale: 2)!
try! png.write(to: URL(fileURLWithPath: CommandLine.arguments[1]))
SWIFT
swiftc -O -o "$TMP/render" Sources/OpenTonic/ControlsView.swift "$TMP/main.swift"
"$TMP/render" "$OUT"
rm -rf "$TMP"
echo "wrote $OUT"
