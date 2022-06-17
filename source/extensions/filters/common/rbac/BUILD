load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    deps = [
        ":engine_lib",
        "//envoy/stats:stats_macros",
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_library(
    name = "matchers_lib",
    srcs = ["matchers.cc"],
    hdrs = [
        "matcher_extension.h",
        "matchers.h",
    ],
    external_deps = ["abseil_optional"],
    deps = [
        "//envoy/http:header_map_interface",
        "//envoy/network:connection_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:matchers_lib",
        "//source/common/config:utility_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/network:cidr_range_lib",
        "//source/extensions/filters/common/expr:evaluator_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/rbac/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "engine_interface",
    hdrs = ["engine.h"],
    deps = [
        "//envoy/http:filter_interface",
        "//envoy/http:header_map_interface",
        "//envoy/network:connection_interface",
    ],
)

envoy_cc_library(
    name = "engine_lib",
    srcs = ["engine_impl.cc"],
    hdrs = ["engine_impl.h"],
    deps = [
        "//source/common/http/matching:data_impl_lib",
        "//source/common/http/matching:inputs_lib",
        "//source/common/matcher:matcher_lib",
        "//source/common/network/matching:inputs_lib",
        "//source/common/ssl/matching:inputs_lib",
        "//source/extensions/filters/common/rbac:engine_interface",
        "//source/extensions/filters/common/rbac:matchers_lib",
        "@envoy_api//envoy/config/rbac/v3:pkg_cc_proto",
    ],
)
