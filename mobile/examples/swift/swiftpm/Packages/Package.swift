// swift-tools-version:5.5

import PackageDescription

// swiftlint:disable line_length

let package = Package(
    name: "NetworkClient",
    platforms: [.iOS(.v15)],
    products: [
        .library(
            name: "NetworkClient",
            targets: ["NetworkClient"]),
    ],
    targets: [
        .target(
            name: "NetworkClient",
            dependencies: ["Envoy"],
            linkerSettings: [
                .linkedLibrary("c++"),
                .linkedLibrary("Network"),
                .linkedFramework("SystemConfiguration"),
            ]
        ),
        // Local xcframework - Must be built locally and moved to the `Packages/` directory before this app
        // can be built
        .binaryTarget(
            name: "Envoy",
            path: "Envoy.xcframework"
        ),
        // GitHub Releases xcframework - Comment the local binary target and uncomment this one to use an
        // official release
        // .binaryTarget(
        //     name: "Envoy",
        //     url: "https://github.com/envoyproxy/envoy-mobile/releases/download/v0.4.6.20220606/Envoy.xcframework.zip",
        //     checksum: "275a5ba8cbac5b206d3f2b9d9555403c74f80e9bba0f0f027c84536ac952c2bf"
        // ),
    ]
)
