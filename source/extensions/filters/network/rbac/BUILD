licenses(["notice"])  # Apache 2

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
        ":rbac_filter",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//envoy/config/filter/network/rbac/v2:rbac_cc",
    ],
)

envoy_cc_library(
    name = "rbac_filter",
    srcs = ["rbac_filter.cc"],
    hdrs = ["rbac_filter.h"],
    deps = [
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:filter_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/extensions/filters/common/rbac:engine_lib",
        "//source/extensions/filters/common/rbac:utility_lib",
        "//source/extensions/filters/network:well_known_names",
    ],
)
