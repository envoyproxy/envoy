licenses(["notice"])  # Apache 2

load("//bazel:aar_with_jni.bzl", "aar_with_jni")
load("//bazel:kotlin_lib.bzl", "envoy_mobile_kt_aar_android_library", "envoy_mobile_kt_library")

aar_with_jni(
    name = "android_aar",
    android_library = "envoy_lib",
    archive_name = "envoy",
    visibility = ["//visibility:public"],
)

envoy_mobile_kt_aar_android_library(
    name = "envoy_lib",
    srcs = [
        "AndroidEnvoyBuilder.kt",
    ],
    custom_package = "io.envoyproxy.envoymobile",
    manifest = "EnvoyManifest.xml",
    visibility = ["//visibility:public"],
    deps = [
        "//library/java/src/io/envoyproxy/envoymobile/engine:envoy_base_engine_lib",
        "//library/java/src/io/envoyproxy/envoymobile/engine:envoy_engine_lib",
        "//library/java/src/io/envoyproxy/envoymobile/engine/types:envoy_c_types_lib",
        "//library/kotlin/src/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)

envoy_mobile_kt_library(
    name = "envoy_interfaces_lib",
    srcs = [
        "Client.kt",
        "Envoy.kt",
        "EnvoyBuilder.kt",
        "EnvoyException.kt",
        "Request.kt",
        "RequestBuilder.kt",
        "RequestMapper.kt",
        "RequestMethod.kt",
        "ResponseHandler.kt",
        "RetryPolicy.kt",
        "RetryPolicyMapper.kt",
        "StreamEmitter.kt",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//library/java/src/io/envoyproxy/envoymobile/engine:envoy_base_engine_lib",
    ],
)
