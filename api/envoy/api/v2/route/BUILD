load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    deps = [
        "//envoy/api/v2/core",
        "//envoy/type",
        "//envoy/type/matcher",
        "//envoy/type/tracing/v2:tracing",
    ],
)

api_proto_library_internal(
    name = "route",
    srcs = ["route.proto"],
    visibility = ["//envoy/api/v2:friends"],
    deps = [
        "//envoy/api/v2/core:base",
        "//envoy/type:percent",
        "//envoy/type:range",
        "//envoy/type/matcher:regex",
        "//envoy/type/matcher:string",
        "//envoy/type/tracing/v2:custom_tag",
    ],
)
