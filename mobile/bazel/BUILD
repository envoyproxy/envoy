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
        "@maven//:junit_junit",
    ],
)

exports_files([
    "pom_template.xml",
])

alias(
    name = "zipper",
    actual = "@bazel_tools//tools/zip:zipper",
)

alias(
    name = "jni",
    actual = "@bazel_tools//tools/jdk:jni",
)
