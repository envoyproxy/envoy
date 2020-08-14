load("@rules_java//java:defs.bzl", "java_library")
load("@build_bazel_rules_android//android:rules.bzl", "android_library")

licenses(["notice"])  # Apache 2

android_library(
    name = "envoy_engine_lib",
    srcs = [
        "AndroidAppLifecycleMonitor.java",
        "AndroidEngineImpl.java",
        "AndroidJniLibrary.java",
        "AndroidNetworkMonitor.java",
    ],
    custom_package = "io.envoyproxy.envoymobile.engine",
    manifest = "AndroidEngineManifest.xml",
    visibility = ["//visibility:public"],
    deps = [
        "//library/java/src/io/envoyproxy/envoymobile/engine:envoy_base_engine_lib",
        "//library/java/src/io/envoyproxy/envoymobile/engine/types:envoy_c_types_lib",
    ],
)

java_library(
    name = "envoy_base_engine_lib",
    srcs = [
        "EnvoyConfiguration.java",
        "EnvoyEngine.java",
        "EnvoyEngineImpl.java",
        "EnvoyHTTPStream.java",
        "JniLibrary.java",
        "JvmBridgeUtility.java",
        "JvmCallbackContext.java",
        "JvmFilterContext.java",
        "JvmFilterFactoryContext.java",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//library/java/src/io/envoyproxy/envoymobile/engine/types:envoy_c_types_lib",
    ],
)

filegroup(
    name = "envoy_base_engine_lib_srcs",
    srcs = ["JvmBridgeUtility.java"],
    visibility = ["//visibility:public"],
)
