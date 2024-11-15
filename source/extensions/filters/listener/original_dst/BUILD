load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# ORIGINAL_DST iptables redirection listener filter
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/listeners/listener_filters/original_dst_filter

envoy_extension_package()

envoy_cc_library(
    name = "original_dst_lib",
    srcs = ["original_dst.cc"],
    hdrs = ["original_dst.h"],
    deps = [
        "//envoy/common:hashable_interface",
        "//envoy/network:filter_interface",
        "//envoy/network:listen_socket_interface",
        "//envoy/stream_info:filter_state_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/config:metadata_lib",
        "//source/common/network:filter_state_dst_address_lib",
        "//source/common/network:upstream_socket_options_filter_state_lib",
        "//source/common/network:utility_lib",
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    extra_visibility = [
        "//test/common/listener_manager:__subpackages__",
    ],
    deps = [
        ":original_dst_lib",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/common/network:filter_state_dst_address_lib",
        "@envoy_api//envoy/extensions/filters/listener/original_dst/v3:pkg_cc_proto",
    ],
)
