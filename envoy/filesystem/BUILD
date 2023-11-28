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
        "//envoy/api:io_error_interface",
        "//envoy/api:os_sys_calls_interface",
        "//envoy/common:time_interface",
        "@com_google_absl//absl/status:statusor",
    ],
)

envoy_cc_library(
    name = "watcher_interface",
    hdrs = ["watcher.h"],
)
