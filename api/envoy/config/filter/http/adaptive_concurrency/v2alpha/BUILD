load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    deps = [
        "//envoy/api/v3alpha/core",
        "//envoy/type",
    ],
)

api_proto_library_internal(
    name = "adaptive_concurrency",
    srcs = ["adaptive_concurrency.proto"],
    deps = [
        "//envoy/api/v3alpha/core:base",
        "//envoy/type:percent",
    ],
)
