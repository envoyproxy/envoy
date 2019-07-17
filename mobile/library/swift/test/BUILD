licenses(["notice"])  # Apache 2

load("@envoy//bazel:envoy_build_system.bzl", "envoy_package")
load("//bazel:swift_test.bzl", "envoy_mobile_swift_test")

envoy_package()

envoy_mobile_swift_test(
    name = "sample_test",
    srcs = [
        "SampleTest.swift",
    ],
)
