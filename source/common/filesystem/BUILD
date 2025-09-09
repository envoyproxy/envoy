load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_cc_platform_dep",
    "envoy_cc_posix_library",
    "envoy_cc_win32_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "directory_lib",
    hdrs = ["directory.h"],
    deps = envoy_cc_platform_dep("directory_iterator_impl_lib"),
)

envoy_cc_win32_library(
    name = "directory_iterator_impl_lib",
    srcs = ["win32/directory_iterator_impl.cc"],
    hdrs = ["win32/directory_iterator_impl.h"],
    include_prefix = "source/common/filesystem",
    strip_include_prefix = "win32",
    deps = [
        "//envoy/common:exception_lib",
        "//envoy/filesystem:filesystem_interface",
        "//source/common/common:fmt_lib",
    ],
)

envoy_cc_posix_library(
    name = "directory_iterator_impl_lib",
    srcs = ["posix/directory_iterator_impl.cc"],
    hdrs = ["posix/directory_iterator_impl.h"],
    include_prefix = "source/common/filesystem",
    strip_include_prefix = "posix",
    deps = [
        "//envoy/filesystem:filesystem_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "filesystem_lib",
    deps = envoy_cc_platform_dep("filesystem_impl_lib"),
)

envoy_cc_posix_library(
    name = "filesystem_impl_lib",
    srcs = ["posix/filesystem_impl.cc"],
    hdrs = ["posix/filesystem_impl.h"],
    include_prefix = "source/common/filesystem",
    strip_include_prefix = "posix",
    deps = [
        ":file_shared_lib",
        "//source/common/runtime:runtime_features_lib",
    ],
)

envoy_cc_win32_library(
    name = "filesystem_impl_lib",
    srcs = ["win32/filesystem_impl.cc"],
    hdrs = ["win32/filesystem_impl.h"],
    include_prefix = "source/common/filesystem",
    strip_include_prefix = "win32",
    deps = [
        ":file_shared_lib",
    ],
)

envoy_cc_library(
    name = "file_shared_lib",
    srcs = ["file_shared_impl.cc"],
    hdrs = ["file_shared_impl.h"],
    deps = [
        "//envoy/filesystem:filesystem_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "watcher_lib",
    srcs = select({
        "//bazel:apple": [
            "kqueue/watcher_impl.cc",
        ],
        "//bazel:windows_x86_64": [
            "win32/watcher_impl.cc",
        ],
        "//conditions:default": [
            "inotify/watcher_impl.cc",
        ],
    }),
    hdrs = select({
        "//bazel:apple": [
            "kqueue/watcher_impl.h",
        ],
        "//bazel:windows_x86_64": [
            "win32/watcher_impl.h",
        ],
        "//conditions:default": [
            "inotify/watcher_impl.h",
        ],
    }),
    include_prefix = "source/common/filesystem",
    strip_include_prefix = select({
        "//bazel:apple": "kqueue",
        "//bazel:windows_x86_64": "win32",
        "//conditions:default": "inotify",
    }),
    deps = [
        "//bazel/foreign_cc:event",
        "//envoy/api:api_interface",
        "//envoy/event:dispatcher_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:linked_object",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/network:default_socket_interface_lib",
    ] + select({
        "//bazel:windows_x86_64": [
            "//source/common/api:os_sys_calls_lib",
            "//source/common/common:thread_lib",
        ],
        "//conditions:default": [],
    }),
)
