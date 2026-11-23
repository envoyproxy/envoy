load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "dynamic_token_bucket_test",
    srcs = ["dynamic_token_bucket_test.cc"],
    deps = [
        "//source/extensions/resource_monitors/adaptive_worker_capacity:dynamic_token_bucket_lib",
        "//test/test_common:simulated_time_system_lib",
        "@abseil-cpp//absl/time",
    ],
)

envoy_cc_test(
    name = "worker_utilization_tracker_test",
    srcs = ["worker_utilization_tracker_test.cc"],
    deps = [
        "//source/extensions/resource_monitors/adaptive_worker_capacity:worker_utilization_tracker_lib",
        "//test/mocks/event:event_mocks",
        "//test/test_common:simulated_time_system_lib",
    ],
)
