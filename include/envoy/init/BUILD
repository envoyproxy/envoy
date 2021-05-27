load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "watcher_interface",
    hdrs = ["watcher.h"],
)

envoy_cc_library(
    name = "target_interface",
    hdrs = ["target.h"],
    deps = [
        ":watcher_interface_no_include_prefix",
    ],
)

envoy_cc_library(
    name = "manager_interface",
    hdrs = ["manager.h"],
    deps = [
        ":target_interface_no_include_prefix",
        ":watcher_interface_no_include_prefix",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
    ],
)
