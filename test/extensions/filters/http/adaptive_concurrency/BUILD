load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "adaptive_concurrency_filter_test",
    srcs = ["adaptive_concurrency_filter_test.cc"],
    extension_names = ["envoy.filters.http.adaptive_concurrency"],
    rbe_pool = "2core",
    deps = [
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/extensions/filters/http/adaptive_concurrency:adaptive_concurrency_filter_lib",
        "//source/extensions/filters/http/adaptive_concurrency/controller:controller_lib",
        "//test/mocks/http:http_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/adaptive_concurrency/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "adaptive_concurrency_filter_integration_test_lib",
    hdrs = ["adaptive_concurrency_filter_integration_test.h"],
)

envoy_extension_cc_test(
    name = "adaptive_concurrency_integration_test",
    size = "large",
    srcs = [
        "adaptive_concurrency_filter_integration_test.cc",
    ],
    extension_names = ["envoy.filters.http.adaptive_concurrency"],
    rbe_pool = "2core",
    deps = [
        ":adaptive_concurrency_filter_integration_test_lib",
        "//source/extensions/filters/http/adaptive_concurrency:config",
        "//source/extensions/filters/http/fault:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
    ],
)
