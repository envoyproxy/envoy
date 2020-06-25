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
    srcs = [
        "direct_response_integration_test.cc",
    ],
    extension_name = "envoy.filters.network.direct_response",
    tags = [
        # TODO(wrowe,sunjayBhatia): Skipping this on Windows as MSVC does not allow invoking
        # connection.close() from within the lambda that is passed to the RawConnectionDriver
        # constructor
        "skip_on_windows",
    ],
    deps = [
        "//source/extensions/filters/network/direct_response:config",
        "//test/integration:integration_lib",
        "//test/test_common:utility_lib",
    ],
)
