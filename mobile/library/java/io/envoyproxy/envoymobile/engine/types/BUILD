load("@build_bazel_rules_android//android:rules.bzl", "android_library")
load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

android_library(
    name = "envoy_c_types_lib",
    srcs = [
        "EnvoyEventTracker.java",
        "EnvoyFinalStreamIntel.java",
        "EnvoyHTTPCallbacks.java",
        "EnvoyHTTPFilter.java",
        "EnvoyHTTPFilterCallbacks.java",
        "EnvoyHTTPFilterFactory.java",
        "EnvoyKeyValueStore.java",
        "EnvoyLogger.java",
        "EnvoyNetworkType.java",
        "EnvoyOnEngineRunning.java",
        "EnvoyStatus.java",
        "EnvoyStreamIntel.java",
        "EnvoyStringAccessor.java",
    ],
    visibility = ["//visibility:public"],
)
