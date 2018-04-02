licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "dispatcher_impl_test",
    srcs = ["dispatcher_impl_test.cc"],
    deps = [
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//test/mocks:common_lib",
    ],
)

envoy_cc_test(
    name = "file_event_impl_test",
    srcs = ["file_event_impl_test.cc"],
    deps = [
        "//include/envoy/event:file_event_interface",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//test/mocks:common_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "dispatched_thread_impl_test",
    srcs = ["dispatched_thread_impl_test.cc"],
    deps = [
        "//source/common/common:utility_lib",
        "//source/common/event:dispatched_thread_lib",
        "//source/server:guarddog_lib",
        "//test/mocks:common_lib",
        "//test/mocks/server:server_mocks",
        "//test/mocks/stats:stats_mocks",
    ],
)
