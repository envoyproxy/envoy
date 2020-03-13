licenses(["notice"])  # Apache 2

# ALTS transport socket. This provides Google's ALTS protocol support in GCP to Envoy.
# https://cloud.google.com/security/encryption-in-transit/application-layer-transport-security/

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
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

envoy_cc_extension(
    name = "config",
    srcs = [
        "config.cc",
    ],
    hdrs = [
        "config.h",
    ],
    security_posture = "robust_to_untrusted_downstream_and_upstream",
    deps = [
        ":tsi_handshaker",
        ":tsi_socket",
        "//include/envoy/registry",
        "//include/envoy/server:transport_socket_config_interface",
        "//source/common/grpc:google_grpc_context_lib",
        "//source/extensions/transport_sockets:well_known_names",
        "@envoy_api//envoy/extensions/transport_sockets/alts/v3:pkg_cc_proto",
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
    name = "tsi_socket",
    srcs = [
        "tsi_socket.cc",
    ],
    hdrs = [
        "tsi_socket.h",
    ],
    deps = [
        ":noop_transport_socket_callbacks_lib",
        ":tsi_frame_protector",
        ":tsi_handshaker",
        "//include/envoy/network:io_handle_interface",
        "//include/envoy/network:transport_socket_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:cleanup_lib",
        "//source/common/common:empty_string",
        "//source/common/common:enum_to_int",
        "//source/common/network:raw_buffer_socket_lib",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_library(
    name = "noop_transport_socket_callbacks_lib",
    hdrs = ["noop_transport_socket_callbacks.h"],
    deps = [
        "//include/envoy/network:io_handle_interface",
        "//include/envoy/network:transport_socket_interface",
    ],
)
