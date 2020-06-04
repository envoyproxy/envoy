load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "api_interface",
    hdrs = ["api.h"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/filesystem:filesystem_interface",
        "//include/envoy/server:process_context_interface",
        "//include/envoy/thread:thread_interface",
    ],
)

envoy_cc_library(
    name = "io_error_interface",
    hdrs = ["io_error.h"],
)

envoy_cc_library(
    name = "os_sys_calls_interface",
    hdrs = [
        "os_sys_calls.h",
        "os_sys_calls_common.h",
        "os_sys_calls_hot_restart.h",
        "os_sys_calls_linux.h",
    ],
)
