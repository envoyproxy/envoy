load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "http1_handshake_lib",
    srcs = ["http1_handshake_initiator.cc"],
    hdrs = ["http1_handshake_initiator.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/http:codec_interface",
        "//envoy/network:address_interface",
        "//envoy/network:filter_interface",
        "//source/common/common:logger_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:response_decoder_impl_base",
        "//source/common/http:utility_lib",
        "//source/common/http/http1:codec_lib",
        "//source/common/network:filter_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/bootstrap/reverse_tunnel/common:handshake_initiator_lib",
        "//source/extensions/bootstrap/reverse_tunnel/common:reverse_connection_utility_lib",
    ],
)

envoy_cc_library(
    name = "http1_handshake_responder_lib",
    srcs = ["http1_handshake_responder.cc"],
    hdrs = ["http1_handshake_responder.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/http:codec_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:logger_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:utility_lib",
        "//source/common/http/http1:codec_lib",
        "//source/common/http/http1:codec_stats_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/bootstrap/reverse_tunnel/common:handshake_responder_lib",
        "//source/extensions/bootstrap/reverse_tunnel/common:reverse_connection_utility_lib",
    ],
)
