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
    name = "integration_test",
    srcs = [
        "integration_test.cc",
    ],
    extension_names = ["envoy.filters.network.set_filter_state"],
    rbe_pool = "2core",
    deps = [
        "//source/common/router:string_accessor_lib",
        "//source/extensions/filters/network/echo:config",
        "//source/extensions/filters/network/set_filter_state:config",
        "//test/integration:integration_lib",
        "//test/integration:utility_lib",
        "//test/test_common:utility_lib",
    ],
)
