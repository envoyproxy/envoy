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
    name = "pattern_template_rewrite_lib",
    srcs = ["pattern_template_rewrite.cc"],
    hdrs = ["pattern_template_rewrite.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/router:router_pattern_template_interface",
        "//source/extensions/pattern_template:pattern_template_lib",
        "@envoy_api//envoy/extensions/pattern_template/rewrite/v3:pkg_cc_proto",
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
        "//envoy/router:router_pattern_template_interface",
        "//source/extensions/pattern_template:pattern_template_lib",
        "@envoy_api//envoy/extensions/pattern_template/rewrite/v3:pkg_cc_proto",
    ],
)
