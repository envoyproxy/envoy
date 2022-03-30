load("@envoy//bazel:envoy_build_system.bzl", "envoy_package")
load("//bazel:flatbuffers.bzl", "envoy_mobile_flatbuffers_library")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_mobile_flatbuffers_library(
    name = "test",
    srcs = ["test.fbs"],
    namespace = "Test.Nested",
    types = [
        "SomeType",
        "SomeOtherType",
    ],
)
