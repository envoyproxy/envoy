load("@build_bazel_rules_apple//apple:apple.bzl", "apple_static_xcframework")
load("@build_bazel_rules_apple//apple:ios.bzl", "ios_static_framework")
load("@build_bazel_rules_swift//swift:swift.bzl", "swift_library")
load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_defines", "envoy_mobile_package")
load("//bazel:config.bzl", "MINIMUM_IOS_VERSION")
load("//bazel:swift_header_collector.bzl", "swift_header_collector")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

swift_library(
    name = "ios_lib",
    srcs = [
        "DirectResponse.swift",
        "Engine.swift",
        "EngineBuilder.swift",
        "EngineImpl.swift",
        "EnvoyError.swift",
        "FinalStreamIntel.swift",
        "Headers.swift",
        "HeadersBuilder.swift",
        "HeadersContainer.swift",
        "KeyValueStore.swift",
        "LogLevel.swift",
        "NetworkMonitoringMode.swift",
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
        "Trailers.swift",
        "extensions/UserDefaults+KeyValueStore.swift",
        "filters/AsyncRequestFilter.swift",
        "filters/AsyncResponseFilter.swift",
        "filters/Filter.swift",
        "filters/FilterDataStatus.swift",
        "filters/FilterHeadersStatus.swift",
        "filters/FilterResumeStatus.swift",
        "filters/FilterTrailersStatus.swift",
        "filters/RequestFilter.swift",
        "filters/RequestFilterCallbacks.swift",
        "filters/RequestFilterCallbacksImpl.swift",
        "filters/ResponseFilter.swift",
        "filters/ResponseFilterCallbacks.swift",
        "filters/ResponseFilterCallbacksImpl.swift",
        "grpc/Data+Extension.swift",
        "grpc/GRPCClient.swift",
        "grpc/GRPCRequestHeaders.swift",
        "grpc/GRPCRequestHeadersBuilder.swift",
        "grpc/GRPCStream.swift",
        "grpc/GRPCStreamPrototype.swift",
        "mocks/MockEnvoyEngine.swift",
        "mocks/MockEnvoyHTTPStream.swift",
        "mocks/MockStream.swift",
        "mocks/MockStreamClient.swift",
        "mocks/MockStreamPrototype.swift",
        "stats/Counter.swift",
        "stats/CounterImpl.swift",
        "stats/Element.swift",
        "stats/Tags.swift",
        "stats/TagsBuilder.swift",
    ] + [
        "@envoy_mobile_extra_swift_sources//:extra_swift_srcs",
    ],
    defines = envoy_mobile_defines("@envoy"),
    features = [
        "swift.emit_symbol_graph",
        "swift.enable_library_evolution",
    ],
    generates_header = True,
    module_name = "Envoy",
    private_deps = [
        "//library/objective-c:envoy_engine_objc_lib",
        "@envoy_mobile_extra_swift_sources//:extra_private_dep",
    ],
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
    minimum_os_version = MINIMUM_IOS_VERSION,
    # Currently the framework is over 2GB, and is not worth caching
    tags = [
        "no-cache",
        "no-remote",
    ],
    visibility = ["//visibility:public"],
    deps = ["ios_lib"],
)

apple_static_xcframework(
    name = "Envoy",
    ios = {
        "simulator": [
            "arm64",
            "x86_64",
        ],
        "device": [
            "arm64",
        ],
    },
    minimum_os_versions = {
        "ios": MINIMUM_IOS_VERSION,
    },
    # Currently the framework is over 2GB, and is not worth caching
    tags = [
        "no-cache",
        "no-remote",
    ],
    visibility = ["//visibility:public"],
    deps = ["ios_lib"],
)
