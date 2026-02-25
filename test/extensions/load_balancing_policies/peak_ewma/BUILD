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
    name = "peak_ewma_lb_integration_test",
    srcs = ["peak_ewma_lb_integration_test.cc"],
    extension_names = ["envoy.load_balancing_policies.peak_ewma"],
    deps = [
        "//source/common/network:address_lib",
        "//source/extensions/load_balancing_policies/peak_ewma:peak_ewma_lb_lib",
        "//test/common/stats:stat_test_utility_lib",
        "//test/mocks:common_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.load_balancing_policies.peak_ewma"],
    deps = [
        "//source/common/protobuf:utility_lib",
        "//source/extensions/load_balancing_policies/peak_ewma:config",
        "//source/extensions/load_balancing_policies/peak_ewma:peak_ewma_lb_lib",
        "//test/common/stats:stat_test_utility_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/load_balancing_policies/peak_ewma/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "cost_test",
    srcs = ["cost_test.cc"],
    extension_names = ["envoy.load_balancing_policies.peak_ewma"],
    deps = [
        "//source/extensions/load_balancing_policies/peak_ewma:peak_ewma_lb_lib",
    ],
)

envoy_extension_cc_test(
    name = "host_data_test",
    srcs = ["host_data_test.cc"],
    extension_names = ["envoy.load_balancing_policies.peak_ewma"],
    deps = [
        "//source/extensions/load_balancing_policies/peak_ewma:peak_ewma_lb_lib",
    ],
)

envoy_extension_cc_test(
    name = "peak_ewma_lb_comprehensive_test",
    srcs = ["peak_ewma_lb_comprehensive_test.cc"],
    extension_names = ["envoy.load_balancing_policies.peak_ewma"],
    deps = [
        "//source/common/network:address_lib",
        "//source/extensions/load_balancing_policies/peak_ewma:peak_ewma_lb_lib",
        "//test/common/stats:stat_test_utility_lib",
        "//test/mocks:common_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)
