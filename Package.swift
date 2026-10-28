// swift-tools-version: 6.4

import PackageDescription

// The game core is portable C (core/, see core/README.md); Swift is only the macOS platform layer.
// -ffp-contract=off: no fused multiply-adds, so the float math rounds as the exe's (x87 in single precision) on every
// platform (tools/exe/oracle.py checks the collision code against the exe this way); keep it in core/Makefile too.
let coreWarnings: [CSetting] = [.unsafeFlags(["-Wall", "-Wextra", "-Wpedantic", "-ffp-contract=off"])]

let package = Package(
    name: "OpenTonic",
    platforms: [.macOS(.v14)],
    targets: [
        // Portable C11 core: .ttlv loader, collision, Ed's controller, animation sampling. No platform code.
        .target(
            name: "TonicCore",
            path: "core",
            exclude: ["tests", "Makefile", "README.md"],
            sources: ["src"],
            publicHeadersPath: "include",
            cSettings: coreWarnings
        ),
        .executableTarget(
            name: "OpenTonic",
            dependencies: ["TonicCore"],
            path: "Sources/OpenTonic",
            resources: [.copy("Shaders.metal")]
        ),
        // Headless checks of the core against converted levels: `swift run -c release CorePlayCheck files/out/ttlv`.
        .executableTarget(
            name: "CorePlayCheck",
            dependencies: ["TonicCore"],
            path: "core/tests",
            sources: ["playcheck.c"],
            cSettings: coreWarnings
        ),
    ],
    cLanguageStandard: .c11
)
