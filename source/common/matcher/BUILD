load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "exact_map_matcher_lib",
    hdrs = ["exact_map_matcher.h"],
    deps = [
        "//include/envoy/matcher:matcher_interface",
    ],
)

envoy_cc_library(
    name = "value_input_matcher_lib",
    hdrs = ["value_input_matcher.h"],
    deps = [
        "//include/envoy/matcher:matcher_interface",
        "//source/common/common:matchers_lib",
    ],
)

envoy_cc_library(
    name = "list_matcher_lib",
    hdrs = ["list_matcher.h"],
    deps = [
        ":field_matcher_lib",
        "//include/envoy/matcher:matcher_interface",
    ],
)

envoy_cc_library(
    name = "field_matcher_lib",
    hdrs = ["field_matcher.h"],
    deps = [
        "//include/envoy/matcher:matcher_interface",
    ],
)

envoy_cc_library(
    name = "matcher_lib",
    srcs = ["matcher.cc"],
    hdrs = ["matcher.h"],
    deps = [
        ":exact_map_matcher_lib",
        ":field_matcher_lib",
        ":list_matcher_lib",
        ":value_input_matcher_lib",
        "//include/envoy/config:typed_config_interface",
        "//include/envoy/matcher:matcher_interface",
        "//source/common/config:utility_lib",
        "@envoy_api//envoy/config/common/matcher/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
