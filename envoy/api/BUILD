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
        "//envoy/common:random_generator_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:scaled_range_timer_manager_interface",
        "//envoy/filesystem:filesystem_interface",
        "//envoy/server:process_context_interface",
        "//envoy/stats:custom_stat_namespaces_interface",
        "//envoy/thread:thread_interface",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
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
    deps = [
        "//envoy/network:address_interface",
        "@com_google_absl//absl/types:optional",
    ],
)
