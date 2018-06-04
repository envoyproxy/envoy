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
        "//include/envoy/registry",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/rbac:rbac_filter_lib",
    ],
)

envoy_cc_library(
    name = "rbac_filter_lib",
    srcs = ["rbac_filter.cc"],
    hdrs = ["rbac_filter.h"],
    deps = [
        "//include/envoy/http:filter_interface",
        "//include/envoy/stats:stats_macros",
        "//source/common/http:utility_lib",
        "//source/extensions/filters/common/rbac:engine_lib",
        "//source/extensions/filters/http:well_known_names",
        "@envoy_api//envoy/config/filter/http/rbac/v2:rbac_cc",
    ],
)
