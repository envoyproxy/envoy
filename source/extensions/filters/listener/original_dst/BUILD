licenses(["notice"])  # Apache 2
# ORIGINAL_DST iptables redirection listener filter
# Public docs: docs/root/configuration/listener_filters/original_dst_filter.rst

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "original_dst_lib",
    srcs = ["original_dst.cc"],
    hdrs = ["original_dst.h"],
    deps = [
        "//include/envoy/network:filter_interface",
        "//include/envoy/network:listen_socket_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "//source/common/network:utility_lib",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    deps = [
        ":original_dst_lib",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/listener:well_known_names",
    ],
)
