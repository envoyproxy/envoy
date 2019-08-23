load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    deps = ["//envoy/api/v2/core"],
)

api_proto_library_internal(
    name = "ip_tagging",
    srcs = ["ip_tagging.proto"],
    deps = ["//envoy/api/v2/core:address"],
)
