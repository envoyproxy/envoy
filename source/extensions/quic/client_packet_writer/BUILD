load(
    "@envoy_build_config//:extensions_build_config.bzl",
    "LEGACY_ALWAYSLINK",
)
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
    "envoy_select_enable_http3",
)

licenses(["notice"])  # Apache 2

# Extensions of QUIC server's preferred address.

envoy_extension_package()

envoy_cc_library(
    name = "default_quic_client_packet_writer_factory_config_lib",
    srcs = envoy_select_enable_http3(["default_quic_client_packet_writer_factory_config.cc"]),
    hdrs = envoy_select_enable_http3(["default_quic_client_packet_writer_factory_config.h"]),
    deps = envoy_select_enable_http3([
        "//envoy/registry",
        "//source/common/quic:envoy_quic_client_packet_writer_factory_interface",
        "//source/common/quic:quic_client_packet_writer_factory_impl_lib",
        "@envoy_api//envoy/extensions/quic/client_writer_factory/v3:pkg_cc_proto",
    ]),
    alwayslink = LEGACY_ALWAYSLINK,
)

envoy_cc_extension(
    name = "default_quic_client_packet_writer_factory_config",
    extra_visibility = [
        "//source/common/quic:__subpackages__",
    ],
    deps = [
        ":default_quic_client_packet_writer_factory_config_lib",
    ],
)
