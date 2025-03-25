load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "client_base",
    hdrs = ["client_base.h"],
    deps = [
        "//envoy/stream_info:stream_info_interface",
    ],
)

envoy_cc_library(
    name = "grpc_client_lib",
    srcs = [
        "grpc_client.cc",
        "grpc_stream.cc",
    ],
    hdrs = [
        "grpc_client.h",
        "grpc_stream.h",
    ],
    tags = ["skip_on_windows"],
    deps = [
        ":client_base",
        "//envoy/grpc:async_client_interface",
        "//envoy/stats:stats_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/grpc:typed_async_client_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
