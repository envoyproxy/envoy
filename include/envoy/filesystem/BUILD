load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "filesystem_interface",
    hdrs = ["filesystem.h"],
    deps = [
        "//include/envoy/api:io_error_interface",
        "//include/envoy/api:os_sys_calls_interface",
        "//include/envoy/event:dispatcher_interface",
    ],
)

envoy_cc_library(
    name = "watcher_interface",
    hdrs = ["watcher.h"],
)
