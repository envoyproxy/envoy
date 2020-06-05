load("@build_bazel_rules_android//android:rules.bzl", "android_binary")
load("@io_bazel_rules_kotlin//kotlin:kotlin.bzl", "kt_android_library")
load("@rules_detekt//detekt:defs.bzl", "detekt")

licenses(["notice"])  # Apache 2

android_binary(
    name = "hello_envoy_kt",
    custom_package = "io.envoyproxy.envoymobile.helloenvoykotlin",
    manifest = "AndroidManifest.xml",
    proguard_specs = ["//library:proguard_rules"],
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
    ],
    deps = [
        "//dist:envoy_mobile_android",
        "//examples/kotlin/shared:hello_envoy_shared_lib",
        "@androidsdk//com.android.support:appcompat-v7-25.0.0",
        "@androidsdk//com.android.support:recyclerview-v7-25.0.0",
    ],
)

detekt(
    name = "hello_envoy_kt_lint",
    srcs = glob(["*.kt"]),
    build_upon_default_config = True,
    cfgs = ["//:kotlin_lint_config"],
)
