licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "callback_lib",
    srcs = ["callback.cc"],
    hdrs = ["callback.h"],
    deps = [
        "//source/common/callback",
        "//source/common/common:logger_lib",
    ],
)

envoy_cc_library(
    name = "init_lib",
    srcs = ["manager_impl.cc"],
    hdrs = ["manager_impl.h"],
    deps = [
        ":callback_lib",
        "//include/envoy/init:init_interface",
        "//source/common/callback:manager",
        "//source/common/common:logger_lib",
    ],
)
