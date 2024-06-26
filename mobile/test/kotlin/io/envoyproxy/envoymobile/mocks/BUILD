load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load("@io_bazel_rules_kotlin//kotlin:android.bzl", "kt_android_library")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

kt_android_library(
    name = "mocks_lib",
    srcs = [
        "MockEnvoyEngine.kt",
        "MockEnvoyHTTPStream.kt",
        "MockStream.kt",
        "MockStreamClient.kt",
        "MockStreamPrototype.kt",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//library/kotlin/io/envoyproxy/envoymobile:envoy_interfaces_lib",
    ],
)
