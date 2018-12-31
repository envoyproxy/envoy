licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "mem_debug_lib",
    srcs = ["mem_debug.cc"],
    hdrs = ["mem_debug.h"],
    deps = ["//source/common/common:assert_lib"],
)

envoy_cc_library(
    name = "stats_lib",
    srcs = ["stats.cc"],
    hdrs = ["stats.h"],
    tcmalloc_dep = 1,
)
