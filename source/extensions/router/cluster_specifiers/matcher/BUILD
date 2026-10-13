load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Matcher cluster specifier plugin.

envoy_extension_package()

envoy_cc_library(
    name = "matcher_cluster_specifier_lib",
    srcs = [
        "matcher_cluster_specifier.cc",
    ],
    hdrs = [
        "matcher_cluster_specifier.h",
    ],
    deps = [
        "//envoy/router:cluster_specifier_plugin_interface",
        "//source/common/common:utility_lib",
        "//source/common/http:utility_lib",
        "//source/common/http/matching:data_impl_lib",
        "//source/common/matcher:matcher_lib",
        "//source/common/router:delegating_route_lib",
        "@envoy_api//envoy/extensions/router/cluster_specifiers/matcher/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":matcher_cluster_specifier_lib",
        "//envoy/registry",
        "//source/common/router:matcher_visitor_lib",
        "@envoy_api//envoy/extensions/router/cluster_specifiers/matcher/v3:pkg_cc_proto",
    ],
)
