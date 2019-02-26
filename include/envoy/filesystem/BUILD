licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "filesystem_interface",
    hdrs = ["filesystem.h"],
    deps = [
        "//include/envoy/api:os_sys_calls_interface",
        "//include/envoy/event:dispatcher_interface",
        "//source/common/filesystem:io_file_error_lib",
    ],
)

envoy_cc_library(
    name = "watcher_interface",
    hdrs = ["watcher.h"],
)
