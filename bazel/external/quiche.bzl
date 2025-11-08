load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_cc_test",
    "envoy_cc_test_library",
)
load("@envoy//bazel:envoy_select.bzl", "envoy_select_enable_http3")

# These options are only used to suppress errors in brought-in QUICHE tests.
# Use #pragma GCC diagnostic ignored in integration code to suppress these errors.
quiche_common_copts = [
    # hpack_huffman_decoder.cc overloads operator<<.
    "-Wno-unused-function",
    "-Wno-old-style-cast",

    # Envoy build should not fail if a dependency has a warning.
    "-Wno-error",
]

quiche_copts = select({
    # Ignore unguarded #pragma GCC statements in QUICHE sources
    "@envoy//bazel:windows_x86_64": ["-wd4068"],
    # Remove these after upstream fix.
    "//conditions:default": quiche_common_copts,
})

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
    )

# Used for QUIC libraries
def envoy_quic_cc_library(
        name,
        srcs = [],
        hdrs = [],
        deps = [],
        defines = [],
        external_deps = [],
        tags = []):
    envoy_cc_library(
        name = name,
        srcs = envoy_select_enable_http3(srcs, "@envoy"),
        hdrs = envoy_select_enable_http3(hdrs, "@envoy"),
        repository = "@envoy",
        copts = quiche_copts,
        tags = tags,
        visibility = ["//visibility:public"],
        defines = defines,
        external_deps = external_deps,
        deps = envoy_select_enable_http3(deps, "@envoy"),
    )

def envoy_quic_cc_test_library(
        name,
        srcs = [],
        hdrs = [],
        tags = [],
        external_deps = [],
        deps = []):
    envoy_cc_test_library(
        name = name,
        srcs = envoy_select_enable_http3(srcs, "@envoy"),
        hdrs = envoy_select_enable_http3(hdrs, "@envoy"),
        copts = quiche_copts,
        repository = "@envoy",
        tags = tags,
        external_deps = external_deps,
        deps = envoy_select_enable_http3(deps, "@envoy"),
    )

def envoy_quic_cc_test(
        name,
        srcs = [],
        tags = [],
        external_deps = [],
        deps = []):
    envoy_cc_test(
        name = name,
        srcs = envoy_select_enable_http3(srcs, "@envoy"),
        copts = quiche_copts,
        repository = "@envoy",
        tags = tags,
        external_deps = external_deps,
        deps = envoy_select_enable_http3(deps, "@envoy"),
    )
