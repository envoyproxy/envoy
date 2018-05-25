licenses(["notice"])  # Apache 2

load("//bazel:api_build_system.bzl", "api_go_proto_library", "api_proto_library")

api_proto_library(
    name = "rbac",
    srcs = ["rbac.proto"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/api/v2/core:address",
        "//envoy/api/v2/route",
        "//envoy/type:string_match",
    ],
)

api_go_proto_library(
    name = "rbac",
    proto = ":rbac",
    deps = [
        "//envoy/api/v2/core:address_go_proto",
        "//envoy/api/v2/route:route_go_proto",
        "//envoy/type:string_match_go_proto",
    ],
)
