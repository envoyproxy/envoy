load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load("@envoy_mobile//bazel:kotlin_lib.bzl", "envoy_mobile_kt_library")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_mobile_kt_library(
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
