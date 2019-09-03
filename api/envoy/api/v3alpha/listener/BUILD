load("@envoy_api//bazel:api_build_system.bzl", "api_go_proto_library", "api_proto_library_internal")

licenses(["notice"])  # Apache 2

api_proto_library_internal(
    name = "listener",
    srcs = ["listener.proto"],
    visibility = ["//envoy/api/v3alpha:friends"],
    deps = [
        "//envoy/api/v3alpha/auth:cert",
        "//envoy/api/v3alpha/core:address",
        "//envoy/api/v3alpha/core:base",
    ],
)

api_go_proto_library(
    name = "listener",
    proto = ":listener",
    deps = [
        "//envoy/api/v3alpha/auth:cert_go_proto",
        "//envoy/api/v3alpha/core:address_go_proto",
        "//envoy/api/v3alpha/core:base_go_proto",
    ],
)

api_proto_library_internal(
    name = "udp_listener_config",
    srcs = ["udp_listener_config.proto"],
    visibility = ["//envoy/api/v3alpha:friends"],
    deps = [
        "//envoy/api/v3alpha/core:base",
    ],
)

api_go_proto_library(
    name = "udp_listener_config",
    proto = ":udp_listener_config",
    deps = [
        "//envoy/api/v3alpha/core:base_go_proto",
    ],
)
