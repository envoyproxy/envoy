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
    name = "watcher_lib",
    srcs = [
        "watcher_impl.cc",
    ],
    hdrs = [
        "watcher_impl.h",
        "watcher_impl_linux.cc",
        "watcher_impl_bsd.cc",
        "watcher_impl_linux.h",
        "watcher_impl_bsd.h",
    ],
    external_deps = ["event"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:linked_object",
        "//source/common/common:logger_lib",
        "//source/common/common:utility_lib",
    ],
)
