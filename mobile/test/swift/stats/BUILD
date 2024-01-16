load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load("@envoy_mobile//bazel:apple.bzl", "envoy_mobile_swift_test")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_mobile_swift_test(
    name = "test",
    srcs = [
        "CounterImplTests.swift",
        "ElementTests.swift",
        "TagsBuilderTests.swift",
    ],
    flaky = True,  # TODO(jpsim): Fix timeouts when running these tests on CI
    tags = ["no-remote-exec"],  # TODO(jpsim): Re-enable remote exec
    visibility = ["//visibility:public"],
    deps = [
        "//library/objective-c:envoy_engine_objc_lib",
    ],
)
