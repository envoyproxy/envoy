load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    name = "route_protos",
    deps = [
        "//envoy/api/v2/core",
        "//envoy/type",
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
    ],
)
