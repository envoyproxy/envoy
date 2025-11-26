load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load("@rules_android//android:rules.bzl", "android_library")
load("@rules_jvm_external//:defs.bzl", "artifact")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

android_library(
    name = "utilities",
    srcs = [
        "ContextUtils.java",
        "StatsUtils.java",
        "StrictModeContext.java",
        "ThreadStatsUid.java",
    ],
    manifest = "UtilitiesManifest.xml",
    visibility = ["//visibility:public"],
    deps = [
        artifact("androidx.annotation:annotation"),
    ],
)

android_library(
    name = "network_utilities",
    srcs = [
        "AndroidCertVerifyResult.java",
        "AndroidNetworkLibrary.java",
        "CertVerifyStatusAndroid.java",
        "FakeX509Util.java",
        "X509Util.java",
    ],
    manifest = "UtilitiesManifest.xml",
    visibility = ["//visibility:public"],
    deps = [
        ":utilities",
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_base_engine_lib",
        "//library/java/io/envoyproxy/envoymobile/engine/types:envoy_c_types_lib",
        artifact("androidx.annotation:annotation"),
    ],
)
