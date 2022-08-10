load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Wildcard & Pattern Rewrites

envoy_extension_package()

envoy_cc_library(
    name = "pattern_template_rewrite_lib",
    srcs = ["pattern_template_rewrite.cc"],
    hdrs = ["pattern_template_rewrite.h"],
    visibility = [
        "//source/common/router:__subpackages__",
        "//test/extensions/path/rewrite/pattern_template:__subpackages__",
    ],
    deps = [
        "//envoy/router:router_path_match_policy_interface",
        "//envoy/router:router_path_rewrite_policy_interface",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/path/match/pattern_template:pattern_template_match_lib",
        "//source/extensions/path/pattern_template_lib",
        "@envoy_api//envoy/extensions/path/match/pattern_template/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/path/rewrite/pattern_template/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    visibility = ["//visibility:public"],
    deps = [
        ":pattern_template_rewrite_lib",
        "//envoy/registry",
        "//envoy/router:router_path_rewrite_policy_interface",
        "//source/extensions/path/pattern_template_lib",
        "@envoy_api//envoy/extensions/path/rewrite/pattern_template/v3:pkg_cc_proto",
    ],
)
