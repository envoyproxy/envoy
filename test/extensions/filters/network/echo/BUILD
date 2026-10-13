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
    name = "echo_integration_test",
    srcs = [
        "echo_integration_test.cc",
    ],
    extension_names = ["envoy.filters.network.echo"],
    rbe_pool = "linux_x64_small",
    tags = [
        # Uncomment this line to run this test repeatedly in exclusive mode if not using docker-sandbox,
        # or RBE, see comments in AddRemoveListener.
        # "exclusive",
        "cpu:2",
    ],
    deps = [
        "//source/extensions/filters/network/echo:config",
        "//test/integration:integration_lib",
        "//test/server:utility_lib",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
    ],
)
