load(
    "//bazel:envoy_build_system.bzl",
    "envoy_benchmark_test",
    "envoy_cc_benchmark_binary",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "manual_executor_lib",
    hdrs = ["manual_executor.h"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/coroutine:executor_lib",
    ],
)

# Core Task / cancellation / leaf / launch tests. These drive leaves manually and
# never create a real timer, so they avoid the dispatcher (and libevent).
envoy_cc_test(
    name = "coroutine_test",
    srcs = ["coroutine_test.cc"],
    rbe_pool = "6gig",
    deps = [
        ":manual_executor_lib",
        "//source/common/coroutine:context_lib",
        "//source/common/coroutine:launch_lib",
        "//source/common/coroutine:leaf_awaitable_lib",
        "//source/common/coroutine:task_lib",
        "//test/test_common:utility_lib",
        "@abseil-cpp//absl/cleanup",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

# DispatcherExecutor + TimerAwaitable, exercised against a simulated-time
# dispatcher.
envoy_cc_test(
    name = "dispatcher_executor_test",
    srcs = ["dispatcher_executor_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/coroutine:dispatcher_executor_lib",
        "//source/common/coroutine:launch_lib",
        "//source/common/coroutine:leaf_awaitable_lib",
        "//source/common/coroutine:task_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
        "@abseil-cpp//absl/status",
    ],
)

# Echo-server micro-benchmark: a callback state machine vs. a coroutine body, both
# over a real dispatcher + IoHandle, driven by a shared blocking two-thread client.
envoy_cc_benchmark_binary(
    name = "perf_test",
    srcs = ["perf_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//envoy/api:api_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:file_event_interface",
        "//envoy/network:io_handle_interface",
        "//envoy/thread:thread_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/coroutine:dispatcher_executor_lib",
        "//source/common/coroutine:launch_lib",
        "//source/common/coroutine:leaf_awaitable_lib",
        "//source/common/coroutine:task_lib",
        "//source/common/network:default_socket_interface_lib",
        "//test/test_common:utility_lib",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@benchmark",
    ],
)

envoy_benchmark_test(
    name = "perf_test_benchmark_test",
    benchmark_binary = "perf_test",
)
