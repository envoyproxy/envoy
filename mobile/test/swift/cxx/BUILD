load("@envoy_mobile//bazel:apple.bzl", "envoy_mobile_swift_test")

licenses(["notice"])  # Apache 2

envoy_mobile_swift_test(
    name = "test",
    srcs = [
        "EnvoyEngineCallbacksFactoryTests.swift",
        "EnvoyEventTrackerFactoryTests.swift",
        "EnvoyHeadersTests.swift",
        "EnvoyLoggerFactoryTests.swift",
        "LogLevelCxxTests.swift",
        "PointerBoxTests.swift",
    ],
    enable_cxx_interop = True,
    visibility = ["//visibility:public"],
    deps = [
        "//library/swift/EnvoyCxxSwiftInterop",
    ],
)
