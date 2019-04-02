licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "api_lib",
    srcs = ["api_impl.cc"],
    hdrs = ["api_impl.h"],
    deps = [
        "//include/envoy/api:api_interface",
        "//source/common/common:thread_lib",
        "//source/common/event:dispatcher_lib",
    ],
)

envoy_cc_library(
    name = "os_sys_calls_lib",
    srcs = ["os_sys_calls_impl.cc"] + select({
        "//bazel:linux_x86_64": ["os_sys_calls_impl_linux.cc"],
        "//bazel:linux_aarch64": ["os_sys_calls_impl_linux.cc"],
        "//conditions:default": [],
    }),
    hdrs = ["os_sys_calls_impl.h"] + select({
        "//bazel:linux_x86_64": ["os_sys_calls_impl_linux.h"],
        "//bazel:linux_aarch64": ["os_sys_calls_impl_linux.h"],
        "//conditions:default": [],
    }),
    deps = [
        "//include/envoy/api:os_sys_calls_interface",
        "//source/common/singleton:threadsafe_singleton",
    ],
)
