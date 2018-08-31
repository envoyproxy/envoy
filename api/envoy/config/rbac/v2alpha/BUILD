licenses(["notice"])  # Apache 2

load("//bazel:api_build_system.bzl", "api_go_proto_library", "api_proto_library_internal")

api_proto_library_internal(
    name = "rbac",
    srcs = ["rbac.proto"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/api/v2/core:address",
        "//envoy/type/matcher:header",
        "//envoy/type/matcher:metadata",
    ],
)

api_go_proto_library(
    name = "rbac",
    proto = ":rbac",
    deps = [
        "//envoy/api/v2/core:address_go_proto",
        "//envoy/type/matcher:header_go_proto",
        "//envoy/type/matcher:metadata_go_proto",
    ],
)
