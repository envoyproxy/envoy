load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Wildcard & Pattern Rewrites

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/registry",
        "//envoy/router:router_path_rewrite_policy_interface",
        "@envoy_api//envoy/extensions/path/rewrite/pattern_template/v3:pkg_cc_proto",
    ],
)
