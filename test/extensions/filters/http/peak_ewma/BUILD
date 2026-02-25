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
    name = "peak_ewma_filter_test",
    srcs = ["peak_ewma_filter_test.cc"],
    extension_names = ["envoy.filters.http.peak_ewma"],
    deps = [
        "//source/extensions/filters/http/peak_ewma:peak_ewma_http_filter_lib",
        "//source/extensions/load_balancing_policies/peak_ewma:peak_ewma_lb_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "peak_ewma_filter_config_test",
    srcs = ["peak_ewma_filter_config_test.cc"],
    extension_names = ["envoy.filters.http.peak_ewma"],
    deps = [
        "//source/extensions/filters/http/peak_ewma:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "integration_test",
    srcs = ["integration_test.cc"],
    extension_names = ["envoy.filters.http.peak_ewma"],
    deps = [
        "//source/extensions/filters/http/peak_ewma:config",
        "//source/extensions/load_balancing_policies/peak_ewma:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
    ],
)
