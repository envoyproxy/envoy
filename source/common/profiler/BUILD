package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_library")

envoy_cc_library(
    name = "profiler_lib",
    srcs = ["profiler.cc"],
    hdrs = ["profiler.h"],
    tcmalloc_dep = 1,
)
