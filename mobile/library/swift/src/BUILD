load("//bazel:swift_static_framework.bzl", "swift_static_framework")

licenses(["notice"])  # Apache 2

swift_static_framework(
    name = "ios_framework",
    srcs = glob([
        "EnvoyClient.swift",
        "EnvoyError.swift",
        "Headers.swift",
        "HeadersBuilder.swift",
        "LogLevel.swift",
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
        "Stream.swift",
        "StreamCallbacks.swift",
        "StreamClient.swift",
        "StreamClientBuilder.swift",
        "StreamPrototype.swift",
        "UpstreamHttpProtocol.swift",
        "filters/*.swift",
        "grpc/*.swift",
    ]),
    module_name = "Envoy",
    private_deps = ["//library/objective-c:envoy_engine_objc_lib"],
    visibility = ["//visibility:public"],
)
