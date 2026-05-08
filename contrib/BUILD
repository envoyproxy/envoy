load("@bazel_skylib//lib:selects.bzl", "selects")
load("@envoy_toolshed//:macros.bzl", "json_data")
load("//bazel:envoy_build_system.bzl", "envoy_cc_test_library", "envoy_contrib_package")
load(":all_contrib_extensions.bzl", "SELECTED_CONTRIB_EXTENSIONS")
load(":contrib_build_config.bzl", "CONTRIB_EXTENSIONS")

licenses(["notice"])  # Apache 2

envoy_contrib_package()

# Disambiguation for SELECTED_CONTRIB_EXTENSIONS' select, where both
# "//bazel:linux_aarch64" and "//bazel:using_aws_lc" otherwise match for an
# arm64 aws-lc build. Restricting the aws-lc branch to x86_64 lets the
# aarch64 branch win on arm64; arm64 + ppc64le aws-lc builds then fall through
# to their platform skip lists, which already exclude the Intel-specific
# contribs that AWS-LC cannot build.
selects.config_setting_group(
    name = "using_aws_lc_on_linux_x86_64",
    match_all = [
        "//bazel:using_aws_lc",
        "//bazel:linux_x86_64",
    ],
    visibility = ["//visibility:public"],
)

exports_files([
    "extensions_metadata.yaml",
    "contrib_build_config.bzl",
])

json_data(
    name = "contrib_extensions_build_config",
    data = CONTRIB_EXTENSIONS,
)

filegroup(
    name = "config_data",
    srcs = select({
        "//bazel:windows_x86_64": [],
        "//conditions:default": [
            "//contrib/golang/filters/http/test/test_data/dummy:testing_shared_objects",
        ],
    }),
    visibility = ["//visibility:public"],
)

envoy_cc_test_library(
    name = "contrib_test_lib",
    deps = [
        "//test/config_test:config_test_lib",
    ] + SELECTED_CONTRIB_EXTENSIONS,
)
