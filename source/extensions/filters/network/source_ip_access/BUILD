licenses(["notice"])  # Apache 2

# Source IP access filter

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "source_ip_access",
    srcs = ["source_ip_access.cc"],
    hdrs = ["source_ip_access.h"],
    deps = [
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/stats:stats_macros",
        "//source/common/network:lc_trie_lib",
        "@envoy_api//envoy/config/filter/network/source_ip_access/v2:source_ip_access_cc",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//include/envoy/registry",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "//source/extensions/filters/network/source_ip_access",
        "@envoy_api//envoy/config/filter/network/source_ip_access/v2:source_ip_access_cc",
    ],
)
