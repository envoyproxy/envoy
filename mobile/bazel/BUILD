load("@envoy//bazel:envoy_build_system.bzl", "envoy_package")
load("@io_bazel_rules_kotlin//kotlin:kotlin.bzl", "kt_jvm_library")

licenses(["notice"])  # Apache 2

envoy_package()

kt_jvm_library(
    name = "envoy_mobile_test_suite",
    srcs = [
        "EnvoyMobileTestSuite.kt",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@maven//:io_github_classgraph_classgraph",
        "@maven//:junit_junit",
    ],
)

config_setting(
    name = "include_ifaddrs",
    values = {"define": "include_ifaddrs=true"},
)

exports_files([
    "pom_template.xml",
    "test_manifest.xml",
])

alias(
    name = "zipper",
    actual = "@bazel_tools//tools/zip:zipper",
)

# Don't depend on these directly, use //library/common/jni:jni_import_lib.
alias(
    name = "jni",
    actual = "@bazel_tools//tools/jdk:jni",
    visibility = ["//library/common/jni/import:__pkg__"],
)

# autoformat requires that there be no @bazel_tools references outside of //bazel,
# so we alias all of the relevant platforms for determining wheel packaging here
alias(
    name = "darwin_arm64",
    actual = "@bazel_tools//src/conditions:darwin_arm64",
)

alias(
    name = "darwin_x86_64",
    actual = "@bazel_tools//src/conditions:darwin_x86_64",
)

alias(
    name = "darwin",
    actual = "@bazel_tools//src/conditions:darwin",
)

alias(
    name = "linux_aarch64",
    actual = "@bazel_tools//src/conditions:linux_aarch64",
)

alias(
    name = "linux_x86_64",
    actual = "@bazel_tools//src/conditions:linux_x86_64",
)
