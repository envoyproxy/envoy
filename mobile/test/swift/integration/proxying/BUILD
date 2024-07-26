load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load("@envoy_mobile//bazel:apple.bzl", "envoy_mobile_swift_test")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_mobile_swift_test(
    name = "http_request_using_proxy_test",
    srcs = [
        "HTTPRequestUsingProxyTest.swift",
    ],
    exec_properties = {
        "sandboxNetwork": "standard",
    },
    visibility = ["//visibility:public"],
    deps = [
        "//library/objective-c:envoy_engine_objc_lib",
        "//test/objective-c:envoy_test_api",
        "//test/objective-c:envoy_test_server",
        "//test/swift/integration:test_extensions",
    ],
)
