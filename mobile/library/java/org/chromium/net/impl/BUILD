load("@build_bazel_rules_android//android:rules.bzl", "android_library")
load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load("@rules_jvm_external//:defs.bzl", "artifact")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

# Android libraries for cronvoy

# An http client backed by Envoy-Mobile.
android_library(
    name = "cronvoy",
    srcs = [
        "Annotations.java",
        "AtomicCombinatoryState.java",
        "CancelProofEnvoyStream.java",
        "CronvoyBidirectionalState.java",
        "CronvoyBidirectionalStream.java",
        "CronvoyBidirectionalStreamBuilderImpl.java",
        "CronvoyBidirectionalStreamNetworkException.java",
        "CronvoyCallbackExceptionImpl.java",
        "CronvoyEngineBase.java",
        "CronvoyEngineBuilderImpl.java",
        "CronvoyExceptionImpl.java",
        "CronvoyImplVersion.java",
        "CronvoyLogger.java",
        "CronvoyMetrics.java",
        "CronvoyNetworkExceptionImpl.java",
        "CronvoyPreconditions.java",
        "CronvoyQuicExceptionImpl.java",
        "CronvoyRequestFinishedInfoImpl.java",
        "CronvoyUploadDataStream.java",
        "CronvoyUrlRequest.java",
        "CronvoyUrlRequestBase.java",
        "CronvoyUrlRequestBuilderImpl.java",
        "CronvoyUrlRequestContext.java",
        "CronvoyUrlResponseInfoImpl.java",
        "CronvoyUserAgent.java",
        "CronvoyVersionSafeCallbacks.java",
        "Errors.java",
        "Executors.java",
        "HttpReason.java",
        "NativeCronvoyEngineBuilderImpl.java",
        "NativeCronvoyEngineBuilderWithLibraryLoaderImpl.java",
        "NativeCronvoyProvider.java",
    ],
    manifest = "CronvoyManifest.xml",
    visibility = ["//visibility:public"],
    deps = [
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_base_engine_lib",
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_engine_lib",
        "//library/java/io/envoyproxy/envoymobile/engine/types:envoy_c_types_lib",
        "//library/java/io/envoyproxy/envoymobile/utilities",
        "//library/java/org/chromium/net",
        "//library/java/org/chromium/net/urlconnection",
        "@maven//:com_google_protobuf_protobuf_javalite",
        artifact("androidx.annotation:annotation"),
    ],
)
