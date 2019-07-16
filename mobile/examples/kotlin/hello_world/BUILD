licenses(["notice"])  # Apache 2

load("@io_bazel_rules_kotlin//kotlin:kotlin.bzl", "kt_android_library")

android_binary(
    name = "hello_envoy_kt",
    custom_package = "io.envoyproxy.envoymobile.helloenvoykotlin",
    manifest = "AndroidManifest.xml",
    deps = [
        "hello_envoy_kt_lib",
    ],
)

kt_android_library(
    name = "hello_envoy_kt_lib",
    srcs = [
        "MainActivity.kt",
    ],
    custom_package = "io.envoyproxy.envoymobile.helloenvoykotlin",
    manifest = "AndroidManifest.xml",
    resource_files = [
        "res/layout/activity_main.xml",
        "res/raw/config.yaml",
    ],
    deps = [
        "//dist:envoy_mobile_android",
        "//examples/kotlin/shared:hello_envoy_shared_lib",
        "@androidsdk//com.android.support:appcompat-v7-25.0.0",
        "@androidsdk//com.android.support:recyclerview-v7-25.0.0",
    ],
)
