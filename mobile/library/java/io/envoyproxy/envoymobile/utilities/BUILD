load("@build_bazel_rules_android//android:rules.bzl", "android_library")
load("@rules_jvm_external//:defs.bzl", "artifact")

licenses(["notice"])  # Apache 2

android_library(
    name = "utilities",
    srcs = glob(["*.java"]),
    manifest = "UtilitiesManifest.xml",
    visibility = ["//visibility:public"],
    deps = [
        artifact("androidx.annotation:annotation"),
    ],
)
