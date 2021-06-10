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
    name = "cache_factory_test",
    srcs = ["cache_factory_test.cc"],
    extension_name = "envoy.filters.network.redis_proxy",
    deps = [
        "//source/extensions/filters/network/redis_proxy/hotkey/cache:cache_factory_lib",
    ],
)
