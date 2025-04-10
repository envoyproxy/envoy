load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "client_base",
    hdrs = ["client_base.h"],
    deps = [
        "//envoy/stream_info:stream_info_interface",
    ],
)

envoy_cc_library(
    name = "grpc_client_interface",
    hdrs = ["grpc_client.h"],
    deps = [
        ":client_base",
    ],
)

envoy_cc_library(
    name = "grpc_client_lib",
    hdrs = [
        "grpc_client_impl.h",
    ],
    tags = ["skip_on_windows"],
    deps = [
        ":grpc_client_interface",
        "//envoy/grpc:async_client_interface",
        "//envoy/stats:stats_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/grpc:typed_async_client_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
