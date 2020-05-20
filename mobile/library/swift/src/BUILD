load("//bazel:swift_static_framework.bzl", "swift_static_framework")

licenses(["notice"])  # Apache 2

swift_static_framework(
    name = "ios_framework",
    srcs = glob([
        "Data+Extension.swift",
        "EnvoyClient.swift",
        "EnvoyClientBuilder.swift",
        "EnvoyError.swift",
        "EnvoyStreamEmitter.swift",
        "HTTPClient.swift",
        "Headers.swift",
        "HeadersBuilder.swift",
        "LogLevel.swift",
        "Request.swift",
        "RequestBuilder.swift",
        "RequestHeaders.swift",
        "RequestHeadersBuilder.swift",
        "RequestMapper.swift",
        "RequestMethod.swift",
        "RequestTrailers.swift",
        "RequestTrailersBuilder.swift",
        "ResponseHandler.swift",
        "ResponseHeaders.swift",
        "ResponseHeadersBuilder.swift",
        "ResponseTrailers.swift",
        "ResponseTrailersBuilder.swift",
        "RetryPolicy.swift",
        "RetryPolicyMapper.swift",
        "StreamEmitter.swift",
        "UpstreamHttpProtocol.swift",
        "filters/*.swift",
        "grpc/*.swift",
    ]),
    module_name = "Envoy",
    objc_includes = [
        "//library/objective-c:EnvoyEngine.h",
    ],
    visibility = ["//visibility:public"],
    deps = ["//library/objective-c:envoy_engine_objc_lib"],
)
