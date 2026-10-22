// swift-tools-version: 6.4

import PackageDescription

// The game core is portable C (core/, see core/README.md); Swift is only the macOS platform layer.
let coreWarnings: [CSetting] = [.unsafeFlags(["-Wall", "-Wextra", "-Wpedantic"])]

let package = Package(
    name: "TonicViewer",
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
            name: "TonicViewer",
            dependencies: ["TonicCore"],
            path: "Sources/TonicViewer",
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
