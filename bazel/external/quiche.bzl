load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_cc_test_library",
)

def envoy_quiche_platform_impl_cc_library(
        name,
        srcs = [],
        hdrs = [],
        deps = []):
    envoy_cc_library(
        name = name,
        srcs = srcs,
        hdrs = hdrs,
        deps = deps,
        repository = "@envoy",
        strip_include_prefix = "quiche/common/platform/default/",
        tags = ["nofips"],
        visibility = ["//visibility:public"],
    )

def envoy_quiche_platform_impl_cc_test_library(
        name,
        srcs = [],
        hdrs = [],
        deps = []):
    envoy_cc_test_library(
        name = name,
        srcs = srcs,
        hdrs = hdrs,
        deps = deps,
        repository = "@envoy",
        strip_include_prefix = "quiche/common/platform/default/",
        tags = ["nofips"],
    )
