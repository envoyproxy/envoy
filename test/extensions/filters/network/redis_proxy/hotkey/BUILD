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
    name = "hotkey_impl_test",
    srcs = ["hotkey_impl_test.cc"],
    extension_names = ["envoy.filters.network.redis_proxy"],
    deps = [
        "//source/extensions/filters/network/redis_proxy/hotkey:hotkey_lib",
        "//test/extensions/filters/network/common/redis:redis_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:utility_lib",
    ],
)
