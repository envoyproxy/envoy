package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_library")

envoy_cc_library(
    name = "assert_lib",
    hdrs = ["assert.h"],
    deps = [":logger_lib"],
)

envoy_cc_library(
    name = "base64_lib",
    srcs = ["base64.cc"],
    hdrs = ["base64.h"],
    deps = [
        ":empty_string",
        "//include/envoy/buffer:buffer_interface",
    ],
)

envoy_cc_library(
    name = "c_smart_ptr_lib",
    hdrs = ["c_smart_ptr.h"],
)

envoy_cc_library(
    name = "empty_string",
    hdrs = ["empty_string.h"],
)

envoy_cc_library(
    name = "enum_to_int",
    hdrs = ["enum_to_int.h"],
)

envoy_cc_library(
    name = "hex_lib",
    srcs = ["hex.cc"],
    hdrs = ["hex.h"],
    deps = [":utility_lib"],
)

envoy_cc_library(
    name = "linked_object",
    hdrs = ["linked_object.h"],
    deps = [":assert_lib"],
)

envoy_cc_library(
    name = "logger_lib",
    srcs = ["logger.cc"],
    hdrs = ["logger.h"],
    deps = [
        ":macros",
        "//include/envoy/thread:thread_interface",
    ],
)

envoy_cc_library(
    name = "macros",
    hdrs = ["macros.h"],
)

envoy_cc_library(
    name = "non_copyable",
    hdrs = ["non_copyable.h"],
)

envoy_cc_library(
    name = "stl_helpers",
    hdrs = ["stl_helpers.h"],
)

envoy_cc_library(
    name = "thread_lib",
    srcs = ["thread.cc"],
    hdrs = ["thread.h"],
    deps = [
        ":assert_lib",
        ":macros",
        "//include/envoy/thread:thread_interface",
    ],
)

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    deps = ["//include/envoy/common:time_interface"],
)

envoy_cc_library(
    name = "version_includes",
    hdrs = ["version.h"],
)

envoy_cc_library(
    name = "version_lib",
    srcs = ["version.cc"],
    deps = [
        ":version_includes",
        "//source/version_generated",
    ],
)
