load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "direct_response_integration_test",
    size = "large",
    srcs = [
        "direct_response_integration_test.cc",
    ],
    extension_names = ["envoy.filters.network.direct_response"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/network/direct_response:config",
        "//test/integration:integration_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "direct_response_test",
    srcs = ["direct_response_test.cc"],
    extension_names = ["envoy.filters.network.direct_response"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/network/direct_response:filter",
        "//test/mocks/api:api_mocks",
        "//test/mocks/network:network_mocks",
        "@envoy_api//envoy/extensions/filters/network/direct_response/v3:pkg_cc_proto",
    ],
)
