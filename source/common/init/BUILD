load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "watcher_lib",
    srcs = ["watcher_impl.cc"],
    hdrs = ["watcher_impl.h"],
    deps = [
        "//include/envoy/init:watcher_interface",
        "//source/common/common:logger_lib",
    ],
)

envoy_cc_library(
    name = "target_lib",
    srcs = ["target_impl.cc"],
    hdrs = ["target_impl.h"],
    deps = [
        "//include/envoy/init:target_interface",
        "//source/common/common:logger_lib",
    ],
)

envoy_cc_library(
    name = "manager_lib",
    srcs = ["manager_impl.cc"],
    hdrs = ["manager_impl.h"],
    deps = [
        ":watcher_lib",
        "//include/envoy/init:manager_interface",
        "//source/common/common:logger_lib",
    ],
)
