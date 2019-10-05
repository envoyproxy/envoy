load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    deps = [
        "//envoy/api/v2/route:pkg",
        "//envoy/type",
    ],
)

api_proto_library_internal(
    name = "health_check",
    srcs = ["health_check.proto"],
    deps = [
        "//envoy/api/v2/route",
        "//envoy/type:percent",
    ],
)
