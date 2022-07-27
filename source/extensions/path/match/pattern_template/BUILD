load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_cc_test",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Wildcard & Pattern Matching

envoy_extension_package()

envoy_cc_library(
    name = "pattern_template_match_lib",
    srcs = ["pattern_template_match.cc"],
    hdrs = ["pattern_template_match.h"],
    visibility = [
        "//source/common/router:__subpackages__",
    ],
    deps = [
        "//envoy/router:router_path_match_policy_interface",
        "//source/extensions/path/pattern_template_lib:pattern_template_lib",
        "@envoy_api//envoy/extensions/path/match/pattern_template/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":pattern_template_match_lib",
        "//envoy/registry",
        "//envoy/router:router_path_match_policy_interface",
        "//source/extensions/path/pattern_template_lib",
        "@envoy_api//envoy/extensions/path/match/pattern_template/v3:pkg_cc_proto",
    ],
)
