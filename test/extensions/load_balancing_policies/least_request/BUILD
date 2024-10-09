load(
    "//bazel:envoy_build_system.bzl",
    "envoy_benchmark_test",
    "envoy_cc_benchmark_binary",
    "envoy_cc_fuzz_test",
    "envoy_package",
    "envoy_proto_library",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.load_balancing_policies.least_request"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/load_balancing_policies/least_request:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:priority_set_mocks",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "integration_test",
    size = "large",
    srcs = ["integration_test.cc"],
    extension_names = ["envoy.load_balancing_policies.least_request"],
    rbe_pool = "2core",
    deps = [
        "//source/common/protobuf",
        "//source/extensions/load_balancing_policies/least_request:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
    ],
)

envoy_proto_library(
    name = "least_request_load_balancer_fuzz_proto",
    srcs = ["least_request_load_balancer_fuzz.proto"],
    deps = [
        "//test/extensions/load_balancing_policies/common:zone_aware_load_balancer_fuzz_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg",
    ],
)

envoy_cc_fuzz_test(
    name = "least_request_load_balancer_fuzz_test",
    srcs = ["least_request_load_balancer_fuzz_test.cc"],
    corpus = "least_request_load_balancer_corpus",
    rbe_pool = "2core",
    deps = [
        ":least_request_load_balancer_fuzz_proto_cc_proto",
        "//source/extensions/load_balancing_policies/least_request:config",
        "//test/common/upstream:utility_lib",
        "//test/extensions/load_balancing_policies/common:zone_aware_load_balancer_fuzz_lib",
    ],
)

envoy_cc_benchmark_binary(
    name = "least_request_lb_benchmark",
    srcs = ["least_request_lb_benchmark.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/load_balancing_policies/least_request:least_request_lb_lib",
        "//test/extensions/load_balancing_policies/common:benchmark_base_tester_lib",
    ],
)

envoy_benchmark_test(
    name = "least_request_lb_benchmark_test",
    timeout = "long",
    benchmark_binary = "least_request_lb_benchmark",
)

envoy_extension_cc_test(
    name = "least_request_lb_test",
    srcs = ["least_request_lb_test.cc"],
    extension_names = ["envoy.load_balancing_policies.least_request"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/load_balancing_policies/least_request:least_request_lb_lib",
        "//test/extensions/load_balancing_policies/common:load_balancer_base_test_lib",
    ],
)

envoy_extension_cc_test(
    name = "least_request_lb_simulation_test",
    srcs = ["least_request_lb_simulation_test.cc"],
    extension_names = ["envoy.load_balancing_policies.least_request"],
    rbe_pool = "2core",
    deps = [
        "//source/common/common:random_generator_lib",
        "//source/common/upstream:load_balancer_context_base_lib",
        "//source/extensions/load_balancing_policies/least_request:least_request_lb_lib",
        "//test/common/upstream:utility_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:host_set_mocks",
        "//test/mocks/upstream:priority_set_mocks",
    ],
)
