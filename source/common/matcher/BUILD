load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "map_matcher_lib",
    hdrs = ["map_matcher.h"],
    deps = [
        "//envoy/matcher:matcher_interface",
    ],
)

envoy_cc_library(
    name = "exact_map_matcher_lib",
    hdrs = ["exact_map_matcher.h"],
    deps = [
        ":map_matcher_lib",
    ],
)

envoy_cc_library(
    name = "prefix_map_matcher_lib",
    hdrs = ["prefix_map_matcher.h"],
    deps = [
        ":map_matcher_lib",
        "//source/common/common:trie_lookup_table_lib",
    ],
)

envoy_cc_library(
    name = "value_input_matcher_lib",
    hdrs = ["value_input_matcher.h"],
    deps = [
        "//envoy/matcher:matcher_interface",
        "//source/common/common:matchers_lib",
    ],
)

envoy_cc_library(
    name = "validation_visitor_lib",
    hdrs = ["validation_visitor.h"],
    deps = [
        "//envoy/matcher:matcher_interface",
    ],
)

envoy_cc_library(
    name = "list_matcher_lib",
    hdrs = ["list_matcher.h"],
    deps = [
        ":field_matcher_lib",
        "//envoy/matcher:matcher_interface",
    ],
)

envoy_cc_library(
    name = "field_matcher_lib",
    hdrs = ["field_matcher.h"],
    deps = [
        "//envoy/matcher:matcher_interface",
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
        ":prefix_map_matcher_lib",
        ":validation_visitor_lib",
        ":value_input_matcher_lib",
        "//envoy/config:typed_config_interface",
        "//envoy/matcher:matcher_interface",
        "//source/common/config:utility_lib",
        "@envoy_api//envoy/config/common/matcher/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
