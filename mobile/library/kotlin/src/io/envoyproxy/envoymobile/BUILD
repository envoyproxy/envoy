licenses(["notice"])  # Apache 2

load("//bazel:aar_with_jni.bzl", "aar_with_jni")
load("//bazel:kotlin_lib.bzl", "envoy_mobile_kt_library")
load("@io_bazel_rules_kotlin//kotlin:kotlin.bzl", "kt_android_library")

aar_with_jni(
    name = "android_aar",
    android_library = ":envoy_lib",
    archive_name = "envoy",
    manifest = "EnvoyManifest.xml",
    native_deps = [
        "//library/common:libenvoy_jni.so",
    ],
    proguard_rules = "//library:proguard_rules",
    visibility = ["//visibility:public"],
)

kt_android_library(
    name = "envoy_lib",
    srcs = [
        "AndroidEnvoyClientBuilder.kt",
    ],
    custom_package = "io.envoyproxy.envoymobile",
    manifest = "EnvoyManifest.xml",
    visibility = ["//visibility:public"],
    deps = [
        ":envoy_interfaces_lib",
        "//library/java/src/io/envoyproxy/envoymobile/engine:envoy_base_engine_lib",
        "//library/java/src/io/envoyproxy/envoymobile/engine:envoy_engine_lib",
        "//library/java/src/io/envoyproxy/envoymobile/engine/types:envoy_c_types_lib",
    ],
)

envoy_mobile_kt_library(
    name = "envoy_interfaces_lib",
    srcs = [
        "Envoy.kt",
        "EnvoyClientBuilder.kt",
        "EnvoyError.kt",
        "EnvoyStreamEmitter.kt",
        "GRPCClient.kt",
        "GRPCRequestBuilder.kt",
        "GRPCResponseHandler.kt",
        "GRPCStreamEmitter.kt",
        "HTTPClient.kt",
        "LogLevel.kt",
        "Request.kt",
        "RequestBuilder.kt",
        "RequestMapper.kt",
        "RequestMethod.kt",
        "ResponseHandler.kt",
        "RetryPolicy.kt",
        "RetryPolicyMapper.kt",
        "StreamEmitter.kt",
        "UpstreamHttpProtocol.kt",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//library/java/src/io/envoyproxy/envoymobile/engine:envoy_base_engine_lib",
    ],
)
