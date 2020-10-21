load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "dispatcher_impl_test",
    srcs = ["dispatcher_impl_test.cc"],
    tags = ["fails_on_windows"],
    deps = [
        "//source/common/api:api_lib",
        "//source/common/event:deferred_task",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/stats:isolated_store_lib",
        "//test/mocks:common_lib",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "file_event_impl_test",
    srcs = ["file_event_impl_test.cc"],
    tags = ["fails_on_windows"],
    deps = [
        "//include/envoy/event:file_event_interface",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/stats:isolated_store_lib",
        "//test/mocks:common_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "scaled_range_timer_manager_impl_test",
    srcs = ["scaled_range_timer_manager_impl_test.cc"],
    deps = [
        "//source/common/event:scaled_range_timer_manager_lib",
        "//test/mocks/event:wrapped_dispatcher",
        "//test/test_common:simulated_time_system_lib",
    ],
)
