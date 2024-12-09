load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
    "envoy_select_admin_functionality",
)

licenses(["notice"])  # Apache 2

# UDP sink extension

envoy_extension_package()

envoy_cc_library(
    name = "udp_sink_impl",
    srcs = ["udp_sink_impl.cc"],
    hdrs = ["udp_sink_impl.h"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/network:udp_packet_writer_handler_lib",
        "//source/common/network:utility_lib",
        "//source/extensions/common/tap:tap_interface",
        "@envoy_api//envoy/config/tap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/tap_sinks/udp_sink/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = envoy_select_admin_functionality(["config.cc"]),
    hdrs = envoy_select_admin_functionality(["config.h"]),
    extra_visibility = [
        # test needs refactor
        "//test/common/tls:__subpackages__",
    ],
    deps = [
        ":udp_sink_impl",
        "//envoy/registry",
        "//source/common/network:utility_lib",
        "//source/extensions/common/tap:tap_interface",
        "@envoy_api//envoy/extensions/tap_sinks/udp_sink/v3:pkg_cc_proto",
    ],
)
