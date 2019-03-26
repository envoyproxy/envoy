licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "watcher_interface",
    hdrs = ["watcher.h"],
)

envoy_cc_library(
    name = "target_interface",
    hdrs = ["target.h"],
    deps = [
        ":watcher_interface",
    ],
)

envoy_cc_library(
    name = "manager_interface",
    hdrs = ["manager.h"],
    deps = [
        ":target_interface",
        ":watcher_interface",
    ],
)
