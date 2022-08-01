load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
)

def envoy_quiche_platform_impl_cc_test_library(
        name,
        srcs = [],
        hdrs = [],
        deps = [],
        tags = [],
        external_deps = []):
    envoy_cc_test_library(
        name = name,
        srcs = srcs,
        hdrs = hdrs,
        deps = deps,
        external_deps = external_deps,
        include_prefix = "quiche_platform_impl",
        tags = tags,
    )
