load("@envoy_mobile//bazel:apple.bzl", "envoy_mobile_swift_test")
load("@envoy_mobile//bazel:envoy_mobile_test_extensions.bzl", "TEST_EXTENSIONS")
load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library")

licenses(["notice"])  # Apache 2

# TODO(jpsim): Fix remote execution for all the tests in this file.

envoy_mobile_swift_test(
    name = "http_request_using_proxy_test",
    srcs = [
        "HTTPRequestUsingProxyTest.swift",
    ],
    tags = [
        "no-remote-exec",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//test/swift/integration:test_extensions",
        "//library/objective-c:envoy_engine_objc_lib",
    ],
)