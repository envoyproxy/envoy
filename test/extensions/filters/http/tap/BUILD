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
    name = "tap_filter_test",
    srcs = ["tap_filter_test.cc"],
    extension_name = "envoy.filters.http.tap",
    deps = [
        "//source/extensions/filters/http/tap:tap_filter_lib",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "tap_filter_integration_test",
    srcs = ["tap_filter_integration_test.cc"],
    extension_name = "envoy.filters.http.tap",
    deps = [
        "//source/extensions/filters/http/tap:config",
        "//test/integration:http_integration_lib",
    ],
)
