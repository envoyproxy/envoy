load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    # TODO(#9953) clean up.
    extra_visibility = [
        "//test/integration:__subpackages__",
    ],
    security_posture = "robust_to_untrusted_downstream",
    deps = [
        "//include/envoy/registry",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/rbac:rbac_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/rbac/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "rbac_filter_lib",
    srcs = ["rbac_filter.cc"],
    hdrs = ["rbac_filter.h"],
    deps = [
        "//include/envoy/http:filter_interface",
        "//include/envoy/stats:stats_macros",
        "//source/common/http:utility_lib",
        "//source/extensions/filters/common/rbac:engine_lib",
        "//source/extensions/filters/common/rbac:utility_lib",
        "//source/extensions/filters/http:well_known_names",
        "@envoy_api//envoy/extensions/filters/http/rbac/v3:pkg_cc_proto",
    ],
)
