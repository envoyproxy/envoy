licenses(["notice"])  # Apache 2

load("@envoy//bazel:envoy_build_system.bzl", "envoy_package")
load("@io_bazel_rules_kotlin//kotlin:kotlin.bzl", "kt_android_library")

envoy_package()

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
    srcs = glob([
        "MainActivity.kt",
        "Response.kt",
        "ResponseRecyclerViewAdapter.kt",
        "ResponseViewHolder.kt",
    ]),
    custom_package = "io.envoyproxy.envoymobile.helloenvoykotlin",
    manifest = "AndroidManifest.xml",
    resource_files = glob([
        "res/**/*.xml",
        "res/**/*.yaml",
    ]),
    deps = [
        "//dist:envoy_mobile_android",
        "@androidsdk//com.android.support:appcompat-v7-25.0.0",
        "@androidsdk//com.android.support:recyclerview-v7-25.0.0",
    ],
)
