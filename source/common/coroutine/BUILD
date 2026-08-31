load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "executor_lib",
    hdrs = ["executor.h"],
    deps = [
        "//envoy/common:pure_lib",
        "//envoy/event:timer_interface",
    ],
)

envoy_cc_library(
    name = "context_lib",
    hdrs = ["context.h"],
    deps = [
        ":executor_lib",
        "@abseil-cpp//absl/functional:any_invocable",
    ],
)

envoy_cc_library(
    name = "status_macros_lib",
    hdrs = ["status_macros.h"],
    deps = [
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

envoy_cc_library(
    name = "task_lib",
    hdrs = ["task.h"],
    deps = [
        ":context_lib",
        ":status_macros_lib",
        "//source/common/common:assert_lib",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

envoy_cc_library(
    name = "leaf_awaitable_lib",
    hdrs = ["leaf_awaitable.h"],
    deps = [
        ":context_lib",
        ":task_lib",
        "//envoy/common:pure_lib",
        "@abseil-cpp//absl/status",
    ],
)

envoy_cc_library(
    name = "launch_lib",
    hdrs = ["launch.h"],
    deps = [
        ":context_lib",
        ":executor_lib",
        ":task_lib",
        "//source/common/common:assert_lib",
        "@abseil-cpp//absl/functional:any_invocable",
    ],
)

envoy_cc_library(
    name = "dispatcher_executor_lib",
    srcs = ["dispatcher_executor.cc"],
    hdrs = ["dispatcher_executor.h"],
    deps = [
        ":executor_lib",
        ":task_lib",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//source/common/common:assert_lib",
    ],
)

envoy_cc_library(
    name = "semaphore_lib",
    srcs = ["semaphore.cc"],
    hdrs = ["semaphore.h"],
    deps = [
        ":context_lib",
        ":launch_lib",
        ":leaf_awaitable_lib",
        ":task_lib",
        "//source/common/common:assert_lib",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

envoy_cc_library(
    name = "async_queue_lib",
    hdrs = ["async_queue.h"],
    deps = [
        ":leaf_awaitable_lib",
        ":semaphore_lib",
        ":status_macros_lib",
        ":task_lib",
        "//source/common/common:assert_lib",
        "@abseil-cpp//absl/functional:any_invocable",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
    ],
)

envoy_cc_library(
    name = "coroutine_lib",
    deps = [
        ":async_queue_lib",
        ":context_lib",
        ":dispatcher_executor_lib",
        ":executor_lib",
        ":launch_lib",
        ":leaf_awaitable_lib",
        ":semaphore_lib",
        ":status_macros_lib",
        ":task_lib",
    ],
)
