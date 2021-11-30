load("@rules_java//java:defs.bzl", "java_library")

licenses(["notice"])  # Apache 2

android_library(
    name = "envoy_engine_lib",
    srcs = [
        "AndroidEngineImpl.java",
        "AndroidJniLibrary.java",
        "AndroidNetworkMonitor.java",
    ],
    custom_package = "io.envoyproxy.envoymobile.engine",
    manifest = "AndroidEngineManifest.xml",
    visibility = ["//visibility:public"],
    deps = [
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_base_engine_lib",
        "//library/java/io/envoyproxy/envoymobile/engine/types:envoy_c_types_lib",
        "@maven//:androidx_core_core",
    ],
)

java_library(
    name = "envoy_base_engine_lib",
    srcs = [
        "EnvoyConfiguration.java",
        "EnvoyEngine.java",
        "EnvoyEngineImpl.java",
        "EnvoyFinalStreamIntelImpl.java",
        "EnvoyHTTPFilterCallbacksImpl.java",
        "EnvoyHTTPStream.java",
        "EnvoyNativeFilterConfig.java",
        "EnvoyNativeResourceRegistry.java",
        "EnvoyNativeResourceReleaser.java",
        "EnvoyNativeResourceWrapper.java",
        "EnvoyStreamIntelImpl.java",
        "JniBridgeUtility.java",
        "JniLibrary.java",
        "JvmBridgeUtility.java",
        "JvmCallbackContext.java",
        "JvmFilterContext.java",
        "JvmFilterFactoryContext.java",
        "JvmStringAccessorContext.java",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//library/java/io/envoyproxy/envoymobile/engine/types:envoy_c_types_lib",
        "@maven//:com_google_code_findbugs_jsr305",
    ],
)

filegroup(
    name = "envoy_base_engine_lib_srcs",
    srcs = ["JvmBridgeUtility.java"],
    visibility = ["//visibility:public"],
)
