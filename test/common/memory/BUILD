licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "debug_test",
    srcs = ["debug_test.cc"],
    deps = ["//source/common/memory:stats_lib"],
)
