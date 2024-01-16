load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
    "envoy_select_enable_http3",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = [
        "config.cc",
    ],
    hdrs = [
        "config.h",
    ],
    extra_visibility = [
        "//source/server:__subpackages__",
        "//source/common/listener_manager:__subpackages__",
    ],
    tags = ["nofips"],
    deps = [
        "//envoy/config:typed_config_interface",
        "//envoy/network:udp_packet_writer_handler_interface",
        "//envoy/registry",
        "@envoy_api//envoy/extensions/udp_packet_writer/v3:pkg_cc_proto",
    ] + envoy_select_enable_http3([
        "//source/common/quic:udp_gso_batch_writer_lib",
    ]),
)
