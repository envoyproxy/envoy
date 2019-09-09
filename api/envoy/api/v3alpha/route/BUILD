load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    deps = [
        "//envoy/api/v3alpha/core",
        "//envoy/type",
        "//envoy/type/matcher",
    ],
)

api_proto_library_internal(
    name = "route",
    srcs = ["route.proto"],
    visibility = ["//envoy/api/v3alpha:friends"],
    deps = [
        "//envoy/api/v3alpha/core:base",
        "//envoy/type:percent",
        "//envoy/type:range",
        "//envoy/type/matcher:regex",
        "//envoy/type/matcher:string",
    ],
)
