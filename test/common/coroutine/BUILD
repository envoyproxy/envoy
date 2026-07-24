load(
    "//bazel:envoy_build_system.bzl",
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
