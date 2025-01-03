load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_library(
    name = "udp_sink_impl",
    srcs = ["udp_sink_impl.cc"],
    hdrs = ["udp_sink_impl.h"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/network:udp_packet_writer_handler_lib",
        "//source/common/network:utility_lib",
        "//source/extensions/common/tap:tap_interface",
        "@envoy_api//contrib/envoy/extensions/tap_sinks/udp_sink/v3alpha:pkg_cc_proto",
        "@envoy_api//envoy/config/tap/v3:pkg_cc_proto",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":udp_sink_impl",
        "//envoy/registry",
        "//source/common/network:utility_lib",
        "//source/extensions/common/tap:tap_interface",
        "@envoy_api//contrib/envoy/extensions/tap_sinks/udp_sink/v3alpha:pkg_cc_proto",
    ],
)
