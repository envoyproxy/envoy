load("@bazel_skylib//rules:common_settings.bzl", "bool_flag")
load("@rules_cc//cc:defs.bzl", "cc_library")
load("//bazel:envoy_build_system.bzl", "envoy_package")

licenses(["notice"])  # Apache 2

exports_files(["icu_data_filter.json"])

envoy_package()

# Global flag to enable parallel builds for all foreign_cc targets
# When enabled, all foreign_cc (or related) builds will use whatever cores available.
# Defaults to false to avoid OOM on developer machines.
bool_flag(
    name = "parallel_builds",
    build_setting_default = False,
)

config_setting(
    name = "parallel_builds_enabled",
    flag_values = {
        ":parallel_builds": "True",
    },
)

cc_library(
    name = "unicode_icu",
    tags = ["skip_on_windows"],
    # Can not be used for the core dataplane due to security concerns
    visibility = ["//contrib/language/filters/http/source:__pkg__"],
    deps = ["@icu"],
)

cc_library(
    name = "libcxx_msan_wrapper",
    visibility = ["//visibility:public"],
    deps = select({
        "//bazel:msan_build": ["@msan_libs"],
        "//conditions:default": [],
    }),
    alwayslink = 1,
)

cc_library(
    name = "libcxx_tsan_wrapper",
    visibility = ["//visibility:public"],
    deps = select({
        "//bazel:tsan_build": ["@tsan_libs"],
        "//conditions:default": [],
    }),
    alwayslink = 1,
)
