licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "conveyor_lib",
    hdrs = ["conveyor.h"],
)

envoy_cc_library(
    name = "log_obj_store_lib",
    hdrs = ["log_obj_store.h"],
    deps = ["//source/common/common:assert_lib"],
)
