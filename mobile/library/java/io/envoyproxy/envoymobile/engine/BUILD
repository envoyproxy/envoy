load("@build_bazel_rules_android//android:rules.bzl", "android_library")
load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

android_library(
    name = "envoy_engine_lib",
    srcs = [
        "AndroidEngineImpl.java",
        "AndroidNetworkMonitor.java",
        "AndroidProxyMonitor.java",
        "UpstreamHttpProtocol.java",
    ],
    custom_package = "io.envoyproxy.envoymobile.engine",
    manifest = "AndroidEngineManifest.xml",
    visibility = ["//visibility:public"],
    deps = [
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_base_engine_lib",
        "//library/java/io/envoyproxy/envoymobile/engine/types:envoy_c_types_lib",
        "//library/java/io/envoyproxy/envoymobile/utilities",
        "@maven//:androidx_annotation_annotation",
        "@maven//:androidx_core_core",
    ],
)

android_library(
    name = "envoy_base_engine_lib",
    srcs = [
        "ByteBuffers.java",
        "EnvoyConfiguration.java",
        "EnvoyEngine.java",
        "EnvoyEngineImpl.java",
        "EnvoyHTTPFilterCallbacksImpl.java",
        "EnvoyHTTPStream.java",
        "EnvoyNativeFilterConfig.java",
        "EnvoyNativeResourceRegistry.java",
        "EnvoyNativeResourceReleaser.java",
        "EnvoyNativeResourceWrapper.java",
        "HeaderMatchConfig.java",
        "JniBridgeUtility.java",
        "JniLibrary.java",
        "JvmFilterContext.java",
        "JvmFilterFactoryContext.java",
        "JvmKeyValueStoreContext.java",
        "JvmStringAccessorContext.java",
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_base_engine_lib_srcs",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//library/java/io/envoyproxy/envoymobile/engine/types:envoy_c_types_lib",
        "@maven//:androidx_annotation_annotation",
        "@maven//:com_google_code_findbugs_jsr305",
        "@maven//:com_google_protobuf_protobuf_javalite",
    ],
)

filegroup(
    name = "envoy_base_engine_lib_srcs",
    srcs = ["JvmBridgeUtility.java"],
    visibility = ["//visibility:public"],
)
