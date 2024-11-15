load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load("@io_bazel_rules_kotlin//kotlin:android.bzl", "kt_android_library")
load("@rules_jvm_external//:defs.bzl", "artifact")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

kt_android_library(
    name = "hello_envoy_shared_lib",
    srcs = [
        "Response.kt",
        "ResponseRecyclerViewAdapter.kt",
        "ResponseViewHolder.kt",
    ],
    custom_package = "io.envoyproxy.envoymobile.shared",
    manifest = "AndroidManifest.xml",
    resource_files = glob([
        "res/layout/item.xml",
        "res/values/colors.xml",
        "res/values/strings.xml",
    ]),
    visibility = ["//visibility:public"],
    deps = [
        artifact("androidx.recyclerview:recyclerview"),
    ],
)
