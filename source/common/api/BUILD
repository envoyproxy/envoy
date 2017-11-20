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
        "//source/common/api:os_sys_calls_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/filesystem:filesystem_lib",
    ],
)

envoy_cc_library(
    name = "os_sys_calls_lib",
    srcs = ["os_sys_calls_impl.cc"],
    hdrs = ["os_sys_calls_impl.h"],
    deps = [
        "//include/envoy/api:os_sys_calls_interface",
        "//source/common/singleton:threadsafe_singleton",
    ],
)
