load("@envoy_mobile//bazel:apple_test.bzl", "envoy_mobile_objc_test")

licenses(["notice"])  # Apache 2

envoy_mobile_objc_test(
    name = "envoy_bridge_utility_test",
    srcs = [
        "EnvoyBridgeUtilityTest.m",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//library/objective-c:envoy_objc_bridge_lib",
    ],
)
