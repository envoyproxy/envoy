licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_mock",
    "envoy_extension_cc_test",
    "envoy_extension_cc_test_library",
)

envoy_package()

envoy_extension_cc_test(
    name = "filter_test",
    srcs = [
        "filter_test.cc",
    ],
    extension_name = "envoy.filters.network.zookeeper_proxy",
    deps = [
        "//source/extensions/filters/network/zookeeper_proxy:config",
        "//test/mocks/network:network_mocks",
    ],
)
