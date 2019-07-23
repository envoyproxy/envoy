licenses(["notice"])  # Apache 2

load("//bazel:swift_static_framework.bzl", "swift_static_framework")

swift_static_framework(
    name = "ios_framework",
    srcs = [
        "Client.swift",
        "Envoy.swift",
        "Error.swift",
        "LogLevel.swift",
        "Request.swift",
        "RequestBuilder.swift",
        "RequestMethod.swift",
        "ResponseHandler.swift",
        "RetryPolicy.swift",
        "RetryPolicyMapper.swift",
        "StreamEmitter.swift",
    ],
    module_name = "Envoy",
    objc_includes = ["//library/objective-c:EnvoyEngine.h"],
    visibility = ["//visibility:public"],
    deps = ["//library/objective-c:envoy_engine_objc_lib"],
)
