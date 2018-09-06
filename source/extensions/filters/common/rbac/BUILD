licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    deps = [
        ":engine_lib",
        "//include/envoy/stats:stats_macros",
        "@envoy_api//envoy/config/filter/http/rbac/v2:rbac_cc",
        "@envoy_api//envoy/config/filter/network/rbac/v2:rbac_cc",
    ],
)

envoy_cc_library(
    name = "matchers_lib",
    srcs = ["matchers.cc"],
    hdrs = ["matchers.h"],
    external_deps = ["abseil_optional"],
    deps = [
        "//include/envoy/http:header_map_interface",
        "//include/envoy/network:connection_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:matchers_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/network:cidr_range_lib",
        "@envoy_api//envoy/api/v2/core:base_cc",
        "@envoy_api//envoy/config/rbac/v2alpha:rbac_cc",
    ],
)

envoy_cc_library(
    name = "engine_interface",
    hdrs = ["engine.h"],
    deps = [
        "//include/envoy/http:filter_interface",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/network:connection_interface",
    ],
)

envoy_cc_library(
    name = "engine_lib",
    srcs = ["engine_impl.cc"],
    hdrs = ["engine_impl.h"],
    deps = [
        "//source/extensions/filters/common/rbac:engine_interface",
        "//source/extensions/filters/common/rbac:matchers_lib",
        "@envoy_api//envoy/api/v2/core:base_cc",
        "@envoy_api//envoy/config/filter/http/rbac/v2:rbac_cc",
    ],
)
