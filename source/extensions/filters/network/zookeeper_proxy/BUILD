licenses(["notice"])  # Apache 2

# ZooKeeper proxy L7 network filter.
# Public docs: docs/root/configuration/network_filters/zookeeper_proxy_filter.rst

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "proxy_lib",
    srcs = [
        "zookeeper_decoder.cc",
        "zookeeper_filter.cc",
        "zookeeper_utils.cc",
    ],
    hdrs = [
        "zookeeper_decoder.h",
        "zookeeper_filter.h",
        "zookeeper_utils.h",
    ],
    deps = [
        "//include/envoy/network:filter_interface",
        "//include/envoy/server:filter_config_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:stats_macros",
        "//source/common/config:filter_json_lib",
        "//source/common/network:filter_lib",
        "//source/extensions/filters/network:well_known_names",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["zookeeper_config.cc"],
    hdrs = ["zookeeper_config.h"],
    deps = [
        ":proxy_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//envoy/config/filter/network/zookeeper_proxy/v1alpha1:zookeeper_proxy_cc",
    ],
)
