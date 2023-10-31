load(
    "//bazel:envoy_build_system.bzl",
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
