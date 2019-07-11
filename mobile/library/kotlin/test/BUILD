licenses(["notice"])  # Apache 2

load("@envoy//bazel:envoy_build_system.bzl", "envoy_package")
load("//bazel:kotlin_test.bzl", "envoy_mobile_kt_test")

envoy_package()

envoy_mobile_kt_test(
    name = "sample_test",
    srcs = [
        "SampleTest.kt",
    ],
)
