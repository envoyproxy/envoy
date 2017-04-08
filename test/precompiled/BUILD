package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_library")

envoy_cc_library(
    name = "precompiled_includes",
    hdrs = ["precompiled_test.h"],
    external_deps = ["googletest"],
    deps = [
        "//test/test_common:printers_includes",
    ],
)
