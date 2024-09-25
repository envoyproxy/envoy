load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "benchmark_base_tester_lib",
    srcs = ["benchmark_base_tester.cc"],
    hdrs = ["benchmark_base_tester.h"],
    deps = [
        "//source/common/common:random_generator_lib",
        "//source/common/memory:stats_lib",
        "//source/common/upstream:upstream_lib",
        "//test/common/upstream:utility_lib",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/test_common:printers_lib",
        "//test/test_common:simulated_time_system_lib",
        "@com_github_google_benchmark//:benchmark",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "bounded_load_hlb_test",
    srcs = ["bounded_load_hlb_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/upstream:upstream_includes",
        "//source/extensions/load_balancing_policies/common:thread_aware_lb_lib",
        "//test/common/upstream:utility_lib",
        "//test/mocks/upstream:upstream_mocks",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "load_balancer_fuzz_lib",
    srcs = ["load_balancer_fuzz_base.cc"],
    hdrs = ["load_balancer_fuzz_base.h"],
    rbe_pool = "2core",
    deps = [
        ":load_balancer_fuzz_proto_cc_proto",
        "//source/common/upstream:load_balancer_context_base_lib",
        "//test/common/upstream:utility_lib",
        "//test/fuzz:random_lib",
        "//test/mocks:common_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:host_set_mocks",
        "//test/mocks/upstream:load_balancer_context_mock",
        "//test/mocks/upstream:priority_set_mocks",
        "//test/test_common:simulated_time_system_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
    ],
)

envoy_proto_library(
    name = "load_balancer_fuzz_proto",
    srcs = ["load_balancer_fuzz.proto"],
    deps = [
        "//test/fuzz:common_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg",
    ],
)

envoy_proto_library(
    name = "zone_aware_load_balancer_fuzz_proto",
    srcs = ["zone_aware_load_balancer_fuzz.proto"],
    deps = [
        "//test/extensions/load_balancing_policies/common:load_balancer_fuzz_proto",
    ],
)

envoy_cc_test_library(
    name = "zone_aware_load_balancer_fuzz_lib",
    srcs = ["zone_aware_load_balancer_fuzz_base.cc"],
    hdrs = ["zone_aware_load_balancer_fuzz_base.h"],
    deps = [
        ":load_balancer_fuzz_lib",
        ":zone_aware_load_balancer_fuzz_proto_cc_proto",
        "//source/extensions/load_balancing_policies/common:load_balancer_lib",
        "//test/mocks/upstream:host_set_mocks",
        "//test/mocks/upstream:priority_set_mocks",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "load_balancer_base_test_lib",
    hdrs = ["load_balancer_impl_base_test.h"],
    deps = [
        "//source/common/common:random_generator_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/load_balancing_policies/common:load_balancer_lib",
        "//test/common/upstream:utility_lib",
        "//test/mocks:common_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/mocks/upstream:host_set_mocks",
        "//test/mocks/upstream:load_balancer_context_mock",
        "//test/mocks/upstream:priority_set_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "load_balancer_base_test",
    srcs = ["load_balancer_impl_base_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":load_balancer_base_test_lib",
    ],
)
