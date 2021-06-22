load("@build_bazel_rules_android//android:rules.bzl", "android_library")
load("@rules_jvm_external//:defs.bzl", "artifact")

licenses(["notice"])  # Apache 2

# Android libraries for cronvoy

# An http client backed by Envoy-Mobile.
android_library(
    name = "cronvoy",
    srcs = [
        "Annotations.java",
        "BidirectionalStreamBuilderImpl.java",
        "CallbackExceptionImpl.java",
        "CronetEngineBase.java",
        "CronetEngineBuilderImpl.java",
        "CronetExceptionImpl.java",
        "CronetMetrics.java",
        "CronetUploadDataStream.java",
        "CronetUrlRequest.java",
        "CronetUrlRequestContext.java",
        "Executors.java",
        "ImplVersion.java",
        "InputStreamChannel.java",
        "JavaCronetEngine.java",
        "JavaCronetEngineBuilderImpl.java",
        "JavaCronetProvider.java",
        "JavaUploadDataSinkBase.java",
        "JavaUrlRequest.java",
        "NativeCronetEngineBuilderImpl.java",
        "NativeCronetEngineBuilderWithLibraryLoaderImpl.java",
        "NativeCronetProvider.java",
        "NetworkExceptionImpl.java",
        "Preconditions.java",
        "QuicExceptionImpl.java",
        "RequestFinishedInfoImpl.java",
        "ThreadStatsUid.java",
        "UrlRequestBase.java",
        "UrlRequestBuilderImpl.java",
        "UrlResponseInfoImpl.java",
        "UserAgent.java",
        "VersionSafeCallbacks.java",
    ],
    manifest = "CronvoyManifest.xml",
    visibility = ["//visibility:public"],
    deps = [
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_base_engine_lib",
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_engine_lib",
        "//library/java/io/envoyproxy/envoymobile/engine/types:envoy_c_types_lib",
        "//library/java/org/chromium/net",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_lib",
        artifact("androidx.annotation:annotation"),
    ],
)
