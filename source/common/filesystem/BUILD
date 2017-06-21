licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "filesystem_lib",
    srcs = ["filesystem_impl.cc"],
    hdrs = ["filesystem_impl.h"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/filesystem:filesystem_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:thread_lib",
    ],
)

envoy_cc_library(
    name = "subscription_lib",
    hdrs = ["subscription_impl.h"],
    external_deps = ["protobuf"],
    deps = [
        ":filesystem_lib",
        "//include/envoy/config:subscription_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/filesystem:filesystem_interface",
        "//source/common/config:utility_lib",
    ],
)

envoy_cc_library(
    name = "watcher_lib",
    srcs = ["watcher_impl.cc"],
    hdrs = ["watcher_impl.h"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:utility_lib",
    ],
)
