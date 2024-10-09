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
    name = "pass_through_filter_test",
    srcs = ["pass_through_filter_test.cc"],
    extension_names = ["envoy.filters.network.thrift_proxy"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/network/thrift_proxy/filters:pass_through_filter_lib",
        "//test/extensions/filters/network/thrift_proxy:mocks",
    ],
)
