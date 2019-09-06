load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    deps = [
        "//envoy/api/v2/auth",
        "//envoy/api/v2/core",
    ],
)

api_proto_library_internal(
    name = "listener",
    srcs = ["listener.proto"],
    visibility = ["//envoy/api/v2:friends"],
    deps = [
        "//envoy/api/v2/auth:cert",
        "//envoy/api/v2/core:address",
        "//envoy/api/v2/core:base",
    ],
)

api_proto_library_internal(
    name = "udp_listener_config",
    srcs = ["udp_listener_config.proto"],
    visibility = ["//envoy/api/v2:friends"],
    deps = [
        "//envoy/api/v2/core:base",
    ],
)
