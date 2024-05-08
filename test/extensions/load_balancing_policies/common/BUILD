load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
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
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "bounded_load_hlb_test",
    srcs = ["bounded_load_hlb_test.cc"],
    deps = [
        "//source/common/upstream:upstream_includes",
        "//source/extensions/load_balancing_policies/common:thread_aware_lb_lib",
        "//test/common/upstream:utility_lib",
        "//test/mocks/upstream:upstream_mocks",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
    ],
)
