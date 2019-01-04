licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "align_lib",
    hdrs = ["align.h"],
)

envoy_cc_library(
    name = "debug_lib",
    srcs = ["debug.cc"],
    hdrs = ["debug.h"],
    deps = [":align_lib"],
)

envoy_cc_library(
    name = "stats_lib",
    srcs = ["stats.cc"],
    hdrs = ["stats.h"],
    tcmalloc_dep = 1,
    deps = [":debug_lib"],
)
