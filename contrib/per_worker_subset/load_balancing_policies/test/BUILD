load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "per_worker_subset_lb_test",
    srcs = ["per_worker_subset_lb_test.cc"],
    deps = [
        "//contrib/per_worker_subset/load_balancing_policies/source:per_worker_subset_lb_lib",
        "//source/common/upstream:upstream_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/mocks/upstream:priority_set_mocks",
        "//test/test_common:simulated_time_system_lib",
    ],
)
