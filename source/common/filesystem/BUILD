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
        "//include/envoy/api:os_sys_calls_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/filesystem:filesystem_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:thread_lib",
    ],
)

envoy_cc_library(
    name = "watcher_lib",
    srcs = select({
        "@bazel_tools//tools/osx:darwin": [
            "kqueue/watcher_impl.cc",
        ],
        "//conditions:default": [
            "inotify/watcher_impl.cc",
        ],
    }),
    hdrs = select({
        "@bazel_tools//tools/osx:darwin": [
            "kqueue/watcher_impl.h",
        ],
        "//conditions:default": [
            "inotify/watcher_impl.h",
        ],
    }),
    external_deps = ["event"],
    strip_include_prefix = select({
        "@bazel_tools//tools/osx:darwin": "kqueue",
        "//conditions:default": "inotify",
    }),
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:linked_object",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:utility_lib",
    ],
)
