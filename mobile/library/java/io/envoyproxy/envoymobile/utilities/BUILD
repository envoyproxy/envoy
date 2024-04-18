load("@build_bazel_rules_android//android:rules.bzl", "android_library")
load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load("@rules_jvm_external//:defs.bzl", "artifact")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

android_library(
    name = "utilities",
    srcs = glob(["*.java"]),
    manifest = "UtilitiesManifest.xml",
    visibility = ["//visibility:public"],
    deps = [
        artifact("androidx.annotation:annotation"),
    ],
)
