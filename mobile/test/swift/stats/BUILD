load("@envoy_mobile//bazel:apple_test.bzl", "envoy_mobile_swift_test")

licenses(["notice"])  # Apache 2

envoy_mobile_swift_test(
    name = "test",
    srcs = [
        "CounterImplTests.swift",
        "ElementTests.swift",
        "TagsBuilderTests.swift",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//library/objective-c:envoy_engine_objc_lib",
    ],
)
