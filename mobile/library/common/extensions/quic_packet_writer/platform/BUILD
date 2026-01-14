load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_proto_library(
    name = "platform_packet_writer_proto",
    srcs = ["platform_packet_writer.proto"],
)

envoy_cc_library(
    name = "platform_packet_writer_factory",
    srcs = ["platform_packet_writer_factory.cc"],
    hdrs = ["platform_packet_writer_factory.h"],
    repository = "@envoy",
    deps = [
        "//library/common/system:system_helper_lib",
        "@envoy//envoy/network:listen_socket_interface",
        "@envoy//source/common/network:udp_packet_writer_handler_lib",
        "@envoy//source/common/quic:envoy_quic_client_packet_writer_factory_interface",
        "@envoy//source/common/quic:envoy_quic_packet_writer_lib",
        "@envoy//source/common/quic:envoy_quic_utils_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    repository = "@envoy",
    deps = [
        ":platform_packet_writer_factory",
        ":platform_packet_writer_proto_cc_proto",
        "@envoy//envoy/registry",
        "@envoy//source/common/quic:envoy_quic_client_packet_writer_factory_interface",
    ],
)
