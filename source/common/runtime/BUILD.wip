package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_library")

envoy_cc_library(
    name = "runtime_lib",
    srcs = ["runtime_impl.cc"],
    hdrs = ["runtime_impl.h"],
    deps = [
        "//include/envoy/common:optional_includes",
        "//include/envoy/event:dispatcher_includes",
        "//include/envoy/runtime:runtime_includes",
        "//include/envoy/stats:stats_includes",
        "//include/envoy/stats:stats_macros",
        "//include/envoy/thread_local:thread_local_includes",
        "//source/common/common:empty_string",
        "//source/common/common:logger_lib",
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
