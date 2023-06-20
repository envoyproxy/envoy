load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
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
    ],
    deps = [
        "//envoy/config:typed_config_interface",
        "//envoy/registry",
        "//source/common/network:udp_packet_writer_handler_lib",
        "@envoy_api//envoy/extensions/udp_packet_writer/v3:pkg_cc_proto",
    ],
)
