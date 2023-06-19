load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "overload_manager_interface",
    hdrs = ["overload_manager.h"],
    deps = [
        ":load_shed_point_interface",
        ":thread_local_overload_state",
        "//envoy/event:dispatcher_interface",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_library(
    name = "thread_local_overload_state",
    hdrs = ["thread_local_overload_state.h"],
    deps = [
        "//envoy/event:scaled_range_timer_manager_interface",
        "//envoy/event:timer_interface",
        "//envoy/server:proactive_resource_monitor_interface",
        "//envoy/thread_local:thread_local_object",
        "//source/common/common:interval_value",
    ],
)

# This is seperate from `:overload_manager_interface` to break
# circular dependencies.
envoy_cc_library(
    name = "load_shed_point_interface",
    hdrs = ["load_shed_point.h"],
)
