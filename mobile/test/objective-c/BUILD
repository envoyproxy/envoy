load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load("@envoy_mobile//bazel:apple.bzl", "envoy_mobile_objc_test", "envoy_objc_library")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_mobile_objc_test(
    name = "envoy_bridge_utility_test",
    srcs = [
        "EnvoyBridgeUtilityTest.m",
    ],
    flaky = True,  # TODO(jpsim): Fix timeouts when running these tests on CI
    tags = ["no-remote-exec"],  # TODO(jpsim): Re-enable remote exec
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
    flaky = True,  # TODO(jpsim): Fix timeouts when running these tests on CI
    tags = ["no-remote-exec"],  # TODO(jpsim): Re-enable remote exec
    visibility = ["//visibility:public"],
    deps = [
        "//library/objective-c:envoy_key_value_store_bridge_impl_lib",
        "//library/objective-c:envoy_objc_bridge_lib",
    ],
)

envoy_objc_library(
    name = "envoy_test_server",
    testonly = True,
    srcs = [
        "EnvoyTestServer.mm",
    ],
    hdrs = ["EnvoyTestServer.h"],
    module_name = "EnvoyTestServer",
    visibility = ["//visibility:public"],
    deps = [
        "//test/common/integration:test_server_interface_lib",
    ],
)
