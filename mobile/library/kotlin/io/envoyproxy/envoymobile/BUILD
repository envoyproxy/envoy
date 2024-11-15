load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load("@envoy_mobile//bazel:android_artifacts.bzl", "android_artifacts")
load("@io_bazel_rules_kotlin//kotlin:android.bzl", "kt_android_library")
load("@rules_detekt//detekt:defs.bzl", "detekt")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

android_artifacts(
    name = "envoy_aar",
    android_library = ":envoy_lib",
    archive_name = "envoy",
    manifest = "EnvoyManifest.xml",
    native_deps = select({
        "@envoy//bazel:opt_build": ["//library/jni:libenvoy_jni.so.debug_info"],
        "//conditions:default": ["//library/jni:libenvoy_jni.so"],
    }),
    proguard_rules = "//library:proguard_rules",
    substitutions = {
        "{pom_artifact_id}": "envoy",
        "{pom_extra_dependencies}": "",
    },
    visibility = ["//visibility:public"],
)

kt_android_library(
    name = "envoy_lib",
    srcs = [
        "AndroidEngineBuilder.kt",
        "android/SharedPreferencesStore.kt",
    ],
    custom_package = "io.envoyproxy.envoymobile",
    manifest = "EnvoyManifest.xml",
    visibility = ["//visibility:public"],
    deps = [
        ":envoy_interfaces_lib",
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_base_engine_lib",
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_engine_lib",
        "//library/java/io/envoyproxy/envoymobile/engine/types:envoy_c_types_lib",
        "//library/java/io/envoyproxy/envoymobile/utilities",
    ],
)

kt_android_library(
    name = "envoy_interfaces_lib",
    srcs = glob([
        "Engine.kt",
        "EngineBuilder.kt",
        "EngineImpl.kt",
        "EnvoyError.kt",
        "Headers.kt",
        "HeadersBuilder.kt",
        "HeadersContainer.kt",
        "KeyValueStore.kt",
        "LogLevel.kt",
        "PulseClient.kt",
        "PulseClientImpl.kt",
        "RequestHeaders.kt",
        "RequestHeadersBuilder.kt",
        "RequestMethod.kt",
        "RequestTrailers.kt",
        "RequestTrailersBuilder.kt",
        "ResponseHeaders.kt",
        "ResponseHeadersBuilder.kt",
        "ResponseTrailers.kt",
        "ResponseTrailersBuilder.kt",
        "RetryPolicy.kt",
        "RetryPolicyMapper.kt",
        "Stream.kt",
        "StreamCallbacks.kt",
        "StreamClient.kt",
        "StreamClientImpl.kt",
        "StreamIntel.kt",
        "FinalStreamIntel.kt",
        "StreamPrototype.kt",
        "StringAccessor.kt",
        "Trailers.kt",
        "filters/*.kt",
        "grpc/*.kt",
        "stats/*.kt",
    ]),
    visibility = ["//visibility:public"],
    deps = [
        "//library/java/io/envoyproxy/envoymobile/engine:envoy_base_engine_lib",
    ],
)

detekt(
    name = "envoy_lib_lint",
    srcs = glob(["*.kt"]),
    build_upon_default_config = True,
    cfgs = ["//:kotlin_lint_config"],
)
