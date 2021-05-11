load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# HTTP filter example with C++20 optional features
# Public docs: docs/root/configuration/http_filters/experimental_compiler_features_filter.rst

envoy_extension_package()

envoy_cc_library(
    name = "experimental_compiler_features_filter_lib",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    copts = ["-std=c++20"],
    deps = [
        "//include/envoy/http:filter_interface",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/experimental_compiler_features/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    category = "envoy.filters.http",
    copts = ["-std=c++20"],
    security_posture = "requires_trusted_downstream_and_upstream",
    status = "alpha",
    deps = [
        ":experimental_compiler_features_filter_lib",
        "//include/envoy/registry",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/experimental_compiler_features/v3:pkg_cc_proto",
    ],
)
