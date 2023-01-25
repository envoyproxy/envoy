load("@envoy_mobile//bazel:apple_test.bzl", "envoy_mobile_objc_test")

licenses(["notice"])  # Apache 2

envoy_mobile_objc_test(
    name = "envoy_bridge_utility_test",
    srcs = [
        "EnvoyBridgeUtilityTest.m",
    ],
    flaky = True,  # TODO(jpsim): Fix timeouts when running these tests on CI
    visibility = ["//visibility:public"],
    deps = [
        "//library/objective-c:envoy_objc_bridge_lib",
    ],
)

envoy_mobile_objc_test(
    name = "envoy_key_value_store_bridge_impl_test",
    srcs = [
        "EnvoyKeyValueStoreBridgeImplTest.m",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//library/objective-c:envoy_key_value_store_bridge_impl_lib",
        "//library/objective-c:envoy_objc_bridge_lib",
    ],
)
