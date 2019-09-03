load("@envoy_api//bazel:api_build_system.bzl", "api_go_proto_library", "api_proto_library_internal")

licenses(["notice"])  # Apache 2

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

api_go_proto_library(
    name = "route",
    proto = ":route",
    deps = [
        "//envoy/api/v3alpha/core:base_go_proto",
        "//envoy/type:percent_go_proto",
        "//envoy/type:range_go_proto",
        "//envoy/type/matcher:regex_go_proto",
        "//envoy/type/matcher:string_go_proto",
    ],
)
