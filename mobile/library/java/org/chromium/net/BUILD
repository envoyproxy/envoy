load("@build_bazel_rules_android//android:rules.bzl", "android_library")
load("@rules_jvm_external//:defs.bzl", "artifact")

licenses(["notice"])  # Apache 2

# Cronet API fork

# Copy of the original Cronet API
android_library(
    name = "net",
    srcs = glob(["*.java"]),
    manifest = "ChromiumNetManifest.xml",
    visibility = ["//visibility:public"],
    deps = [
        artifact("androidx.annotation:annotation"),
    ],
)

# A binary target that includes transitive dependencies.
# Use this target along with libenvoy_jni.so to integrate
# Cronvoy with Cronet-dependent apps.
android_binary(
    name = "cronet",
    srcs = [],
    manifest = "ChromiumNetManifest.xml",
    proguard_specs = ["//library:proguard_rules"],
    visibility = ["//visibility:public"],
    deps = [
        ":net",
        "//library/java/org/chromium/net/impl:cronvoy",
    ],
)
