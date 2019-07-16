licenses(["notice"])  # Apache 2

load("@io_bazel_rules_kotlin//kotlin:kotlin.bzl", "kt_android_library")

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
        "@androidsdk//com.android.support:recyclerview-v7-25.0.0",
    ],
)
