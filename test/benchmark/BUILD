load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "main",
    srcs = ["main.cc"],
    hdrs = ["main.h"],
    external_deps = [
        "benchmark",
        "tclap",
    ],
    deps = [
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:thread_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:test_runtime_lib",
    ],
)
