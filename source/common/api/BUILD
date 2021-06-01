load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
    "envoy_select_hot_restart",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "api_lib",
    srcs = ["api_impl.cc"],
    hdrs = ["api_impl.h"],
    deps = [
        "//include/envoy/api:api_interface",
        "//source/common/common:thread_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:socket_lib",
    ],
)

envoy_cc_library(
    name = "os_sys_calls_lib",
    srcs = select({
        "//bazel:linux": [
            "posix/os_sys_calls_impl.cc",
            "posix/os_sys_calls_impl_linux.cc",
        ],
        "//bazel:windows_x86_64": ["win32/os_sys_calls_impl.cc"],
        "//conditions:default": ["posix/os_sys_calls_impl.cc"],
    }) + envoy_select_hot_restart(["posix/os_sys_calls_impl_hot_restart.cc"]),
    hdrs = select({
        "//bazel:linux": [
            "posix/os_sys_calls_impl.h",
            "posix/os_sys_calls_impl_linux.h",
        ],
        "//bazel:windows_x86_64": ["win32/os_sys_calls_impl.h"],
        "//conditions:default": ["posix/os_sys_calls_impl.h"],
    }) + envoy_select_hot_restart(["posix/os_sys_calls_impl_hot_restart.h"]),
    strip_include_prefix = select({
        "//bazel:windows_x86_64": "win32",
        "//conditions:default": "posix",
    }),
    deps = [
        "//include/envoy/api:os_sys_calls_interface",
        "//source/common/singleton:threadsafe_singleton",
    ],
)
