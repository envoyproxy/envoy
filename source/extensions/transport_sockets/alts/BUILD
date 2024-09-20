load("@rules_proto_grpc//cpp:defs.bzl", "cpp_grpc_library")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# ALTS transport socket. This provides Google's ALTS protocol support in GCP to Envoy.
# https://cloud.google.com/security/encryption-in-transit/application-layer-transport-security/

envoy_extension_package()

envoy_cc_library(
    name = "grpc_tsi_wrapper",
    hdrs = [
        "grpc_tsi.h",
    ],
    external_deps = ["grpc"],
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
    deps = [
        ":alts_channel_pool",
        ":tsi_handshaker",
        ":tsi_socket",
        "//envoy/registry",
        "//envoy/server:transport_socket_config_interface",
        "//source/common/grpc:google_grpc_context_lib",
        "@com_google_absl//absl/container:node_hash_set",
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
        ":alts_tsi_handshaker",
        ":grpc_tsi_wrapper",
        "//envoy/event:dispatcher_interface",
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
        "//envoy/network:io_handle_interface",
        "//envoy/network:transport_socket_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/buffer:watermark_buffer_lib",
        "//source/common/common:cleanup_lib",
        "//source/common/common:empty_string",
        "//source/common/common:enum_to_int",
        "//source/common/network:raw_buffer_socket_lib",
        "//source/common/network:transport_socket_options_lib",
        "//source/common/protobuf:utility_lib",
    ],
)

envoy_cc_library(
    name = "noop_transport_socket_callbacks_lib",
    hdrs = ["noop_transport_socket_callbacks.h"],
    deps = [
        "//envoy/network:io_handle_interface",
        "//envoy/network:transport_socket_interface",
    ],
)

envoy_cc_library(
    name = "alts_channel_pool",
    srcs = ["alts_channel_pool.cc"],
    hdrs = ["alts_channel_pool.h"],
    external_deps = ["grpc"],
    deps = [
        "@com_google_absl//absl/random",
    ],
)

cpp_grpc_library(
    name = "handshaker_cc_grpc",
    protos = ["@com_github_grpc_grpc//src/proto/grpc/gcp:alts_handshaker_proto"],
)

envoy_cc_library(
    name = "alts_proxy",
    srcs = ["alts_proxy.cc"],
    hdrs = ["alts_proxy.h"],
    external_deps = ["grpc"],
    deps = [
        ":handshaker_cc_grpc",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/status:statusor",
    ],
)

envoy_cc_library(
    name = "alts_tsi_handshaker",
    srcs = ["alts_tsi_handshaker.cc"],
    hdrs = ["alts_tsi_handshaker.h"],
    external_deps = ["grpc"],
    deps = [
        ":alts_proxy",
        ":handshaker_cc_grpc",
        ":tsi_frame_protector",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/status",
    ],
)
