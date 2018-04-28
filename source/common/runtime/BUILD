licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "runtime_lib",
    srcs = ["runtime_impl.cc"],
    hdrs = ["runtime_impl.h"],
    external_deps = ["ssl"],
    deps = [
        "//include/envoy/api:os_sys_calls_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:stats_macros",
        "//include/envoy/thread_local:thread_local_interface",
        "//source/common/common:empty_string",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:thread_lib",
        "//source/common/common:utility_lib",
        "//source/common/filesystem:filesystem_lib",
    ],
)

envoy_cc_library(
    name = "uuid_util_lib",
    srcs = ["uuid_util.cc"],
    hdrs = ["uuid_util.h"],
    deps = [
        ":runtime_lib",
        "//source/common/common:utility_lib",
    ],
)
