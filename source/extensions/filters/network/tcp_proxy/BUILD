licenses(["notice"])  # Apache 2
# TCP proxy L4 network filter.
# Public docs: docs/root/configuration/network_filters/tcp_proxy_filter.rst

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//include/envoy/registry",
        "//source/common/config:filter_json_lib",
        "//source/common/tcp_proxy",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
    ],
)
