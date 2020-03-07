licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
)

envoy_package()

envoy_cc_test(
    name = "direct_response_integration_test",
    srcs = [
        "direct_response_integration_test.cc",
    ],
    deps = [
        "//source/extensions/filters/network/direct_response:config",
        "//test/integration:integration_lib",
        "//test/test_common:utility_lib",
    ],
)
