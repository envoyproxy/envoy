load("@build_bazel_rules_android//android:rules.bzl", "android_library")
load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load("@rules_jvm_external//:defs.bzl", "artifact")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

# Cronet API fork

# Copy of the original Cronet API
android_library(
    name = "net",
    srcs = glob(["*.java"]),
    manifest = "ChromiumNetManifest.xml",
    visibility = ["//visibility:public"],
    deps = [
        "//library/java/io/envoyproxy/envoymobile/utilities",
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
        artifact("com.google.protobuf:protobuf-javalite"),
    ],
)
