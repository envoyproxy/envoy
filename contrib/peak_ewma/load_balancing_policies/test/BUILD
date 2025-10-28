load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "peak_ewma_lb_integration_test",
    srcs = ["peak_ewma_lb_integration_test.cc"],
    deps = [
        "//contrib/peak_ewma/load_balancing_policies/source:peak_ewma_lb_lib",
        "//source/common/network:address_lib",
        "//test/common/stats:stat_test_utility_lib",
        "//test/mocks:common_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    deps = [
        "//contrib/peak_ewma/load_balancing_policies/source:config",
        "//contrib/peak_ewma/load_balancing_policies/source:peak_ewma_lb_lib",
        "//source/common/protobuf:utility_lib",
        "//test/common/stats:stat_test_utility_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//contrib/envoy/extensions/load_balancing_policies/peak_ewma/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "cost_test",
    srcs = ["cost_test.cc"],
    deps = [
        "//contrib/peak_ewma/load_balancing_policies/source:peak_ewma_lb_lib",
    ],
)

envoy_cc_test(
    name = "host_data_test",
    srcs = ["host_data_test.cc"],
    deps = [
        "//contrib/peak_ewma/load_balancing_policies/source:peak_ewma_lb_lib",
    ],
)

envoy_cc_test(
    name = "peak_ewma_lb_comprehensive_test",
    srcs = ["peak_ewma_lb_comprehensive_test.cc"],
    deps = [
        "//contrib/peak_ewma/load_balancing_policies/source:peak_ewma_lb_lib",
        "//source/common/network:address_lib",
        "//test/common/stats:stat_test_utility_lib",
        "//test/mocks:common_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)
