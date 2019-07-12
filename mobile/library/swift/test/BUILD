licenses(["notice"])  # Apache 2

load("@envoy//bazel:envoy_build_system.bzl", "envoy_package")
load("@build_bazel_rules_swift//swift:swift.bzl", "swift_test")

envoy_package()

swift_test(
    name = "sample_test",
    srcs = [
        "SampleTest.swift",
    ],
)
