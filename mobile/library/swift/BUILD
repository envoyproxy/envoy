load("@build_bazel_rules_apple//apple:ios.bzl", "ios_static_framework")
load("@build_bazel_rules_swift//swift:swift.bzl", "swift_library")
load("//bazel:swift_header_collector.bzl", "swift_header_collector")

licenses(["notice"])  # Apache 2

swift_library(
    name = "ios_lib",
    srcs = glob([
        "DirectResponse.swift",
        "Engine.swift",
        "EngineBuilder.swift",
        "EngineImpl.swift",
        "EnvoyError.swift",
        "Headers.swift",
        "HeadersBuilder.swift",
        "LogLevel.swift",
        "PulseClient.swift",
        "PulseClientImpl.swift",
        "RequestHeaders.swift",
        "RequestHeadersBuilder.swift",
        "RequestMethod.swift",
        "RequestTrailers.swift",
        "RequestTrailersBuilder.swift",
        "ResponseHeaders.swift",
        "ResponseHeadersBuilder.swift",
        "ResponseTrailers.swift",
        "ResponseTrailersBuilder.swift",
        "RetryPolicy.swift",
        "RetryPolicyMapper.swift",
        "RouteMatcher.swift",
        "Stream.swift",
        "StreamCallbacks.swift",
        "StreamClient.swift",
        "StreamClientImpl.swift",
        "StreamIntel.swift",
        "StreamPrototype.swift",
        "TestEngineBuilder.swift",
        "Trailers.swift",
        "UpstreamHttpProtocol.swift",
        "filters/*.swift",
        "grpc/*.swift",
        "mocks/*.swift",
        "stats/*.swift",
    ]) + ["@envoy_mobile_extra_swift_sources//:extra_swift_srcs"],
    features = ["swift.enable_library_evolution"],
    module_name = "Envoy",
    private_deps = ["//library/objective-c:envoy_engine_objc_lib"],
    visibility = ["//visibility:public"],
)

swift_header_collector(
    name = "ios_lib_headers",
    library = "ios_lib",
)

ios_static_framework(
    name = "ios_framework",
    hdrs = ["ios_lib_headers"],
    bundle_name = "Envoy",
    minimum_os_version = "11.0",
    visibility = ["//visibility:public"],
    deps = ["ios_lib"],
)
