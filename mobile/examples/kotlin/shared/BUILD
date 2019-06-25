licenses(["notice"])  # Apache 2

load("@envoy//bazel:envoy_build_system.bzl", "envoy_package")
load("@io_bazel_rules_kotlin//kotlin:kotlin.bzl", "kt_android_library")

envoy_package()

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
    deps = [
        "@androidsdk//com.android.support:recyclerview-v7-25.0.0",
    ],
)
