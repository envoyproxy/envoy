load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library_internal", "api_proto_package")

licenses(["notice"])  # Apache 2

api_proto_package(
    deps = [
        "//envoy/api/v3alpha/auth",
        "//envoy/api/v3alpha/core",
    ],
)

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

api_proto_library_internal(
    name = "udp_listener_config",
    srcs = ["udp_listener_config.proto"],
    visibility = ["//envoy/api/v3alpha:friends"],
    deps = [
        "//envoy/api/v3alpha/core:base",
    ],
)

api_proto_library_internal(
    name = "quic_config",
    srcs = ["quic_config.proto"],
    visibility = ["//envoy/api/v3alpha:friends"],
)
