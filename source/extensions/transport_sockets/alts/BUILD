licenses(["notice"])  # Apache 2

# TODO(lizan): Add link to docs when it is ready.

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "grpc_tsi_wrapper",
    hdrs = [
        "grpc_tsi.h",
    ],
    external_deps = [
        "grpc",
    ],
    visibility = ["//visibility:private"],
    deps = [
        "//source/common/common:c_smart_ptr_lib",
    ],
)

envoy_cc_library(
    name = "tsi_frame_protector",
    srcs = [
        "tsi_frame_protector.cc",
    ],
    hdrs = [
        "tsi_frame_protector.h",
    ],
    repository = "@envoy",
    deps = [
        ":grpc_tsi_wrapper",
        "//source/common/buffer:buffer_lib",
    ],
)

envoy_cc_library(
    name = "tsi_handshaker",
    srcs = [
        "tsi_handshaker.cc",
    ],
    hdrs = [
        "tsi_handshaker.h",
    ],
    deps = [
        ":grpc_tsi_wrapper",
        "//include/envoy/event:dispatcher_interface",
        "//source/common/buffer:buffer_lib",
    ],
)

envoy_cc_library(
    name = "noop_transport_socket_callbacks_lib",
    hdrs = ["noop_transport_socket_callbacks.h"],
    deps = ["//include/envoy/network:transport_socket_interface"],
)
