load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "dns_gateway_filter_lib",
    srcs = ["dns_gateway_filter.cc"],
    hdrs = [
        "dns_gateway_filter.h",
    ],
    deps = [
        "//envoy/network:filter_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "//source/common/network:utility_lib",
        "//source/common/stats:stats_lib",
        "//source/extensions/common/synthetic_ip:cache_manager_lib",
        "//source/extensions/filters/udp/dns_filter:dns_filter_lib",
        "@envoy_api//envoy/extensions/filters/udp/dns_gateway/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":dns_gateway_filter_lib",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/common/synthetic_ip:cache_manager_lib",
        "@envoy_api//envoy/extensions/filters/udp/dns_gateway/v3:pkg_cc_proto",
    ],
)
