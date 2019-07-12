licenses(["notice"])  # Apache 2

load("@envoy//bazel:envoy_build_system.bzl", "envoy_package")

envoy_package()

load("//bazel:swift_static_framework.bzl", "swift_static_framework")

swift_static_framework(
    name = "ios_framework",
    srcs = [
        "Envoy.swift",
        "LogLevel.swift",
        "Request.swift",
        "RequestBuilder.swift",
        "RequestMethod.swift",
        "RetryPolicy.swift",
    ],
    module_name = "Envoy",
    objc_includes = ["//library/objective-c:EnvoyEngine.h"],
    visibility = ["//visibility:public"],
    deps = ["//library/objective-c:envoy_engine_objc_lib"],
)
