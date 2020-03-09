licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "direct_response_integration_test",
    srcs = [
        "direct_response_integration_test.cc",
    ],
    extension_name = "envoy.filters.network.direct_response",
    deps = [
        "//source/extensions/filters/network/direct_response:config",
        "//test/integration:integration_lib",
        "//test/test_common:utility_lib",
    ],
)
