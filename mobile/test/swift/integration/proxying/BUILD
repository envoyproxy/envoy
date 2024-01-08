load("@envoy_mobile//bazel:apple.bzl", "envoy_mobile_swift_test")

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
        "//library/objective-c:envoy_engine_objc_lib",
        "//test/objective-c:envoy_test_server",
        "//test/swift/integration:test_extensions",
    ],
)
