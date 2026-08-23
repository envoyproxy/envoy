load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Weighted priority cluster specifier plugin.

envoy_extension_package()

envoy_cc_library(
    name = "weighted_priority_cluster_specifier_lib",
    srcs = [
        "weighted_priority_cluster_specifier.cc",
    ],
    hdrs = [
        "weighted_priority_cluster_specifier.h",
    ],
    deps = [
        "//envoy/router:cluster_specifier_plugin_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:logger_lib",
        "//source/common/router:delegating_route_lib",
        "@envoy_api//envoy/extensions/router/cluster_specifiers/weighted_priority/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":weighted_priority_cluster_specifier_lib",
        "//envoy/registry",
        "@envoy_api//envoy/extensions/router/cluster_specifiers/weighted_priority/v3:pkg_cc_proto",
    ],
)
