load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Header weighted cluster specifier plugin.

envoy_extension_package()

envoy_cc_library(
    name = "header_weighted_cluster_specifier_lib",
    srcs = [
        "header_weighted.cc",
    ],
    hdrs = [
        "header_weighted.h",
    ],
    deps = [
        "//envoy/router:cluster_specifier_plugin_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/router:delegating_route_lib",
        "@envoy_api//envoy/extensions/router/cluster_specifiers/header_weighted/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":header_weighted_cluster_specifier_lib",
        "//envoy/registry",
    ],
)
