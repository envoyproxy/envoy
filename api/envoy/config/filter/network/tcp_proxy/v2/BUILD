load("@envoy_api//bazel:api_build_system.bzl", "api_go_proto_library", "api_proto_library_internal")

licenses(["notice"])  # Apache 2

api_proto_library_internal(
    name = "tcp_proxy",
    srcs = ["tcp_proxy.proto"],
    deps = [
        "//envoy/api/v2/core:address",
        "//envoy/api/v2/core:base",
        "//envoy/config/filter/accesslog/v2:accesslog",
    ],
)

api_go_proto_library(
    name = "tcp_proxy",
    proto = ":tcp_proxy",
    deps = [
        "//envoy/api/v2/core:address_go_proto",
        "//envoy/api/v2/core:base_go_proto",
        "//envoy/config/filter/accesslog/v2:accesslog_go_proto",
    ],
)
