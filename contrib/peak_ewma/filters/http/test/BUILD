load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "peak_ewma_filter_test",
    srcs = ["peak_ewma_filter_test.cc"],
    deps = [
        "//contrib/peak_ewma/filters/http/source:peak_ewma_http_filter_lib",
        "//contrib/peak_ewma/load_balancing_policies/source:peak_ewma_lb_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "peak_ewma_filter_config_test",
    srcs = ["peak_ewma_filter_config_test.cc"],
    deps = [
        "//contrib/peak_ewma/filters/http/source:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "integration_test",
    srcs = ["integration_test.cc"],
    deps = [
        "//contrib/peak_ewma/filters/http/source:config",
        "//contrib/peak_ewma/load_balancing_policies/source:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
    ],
)
