load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "fatal_error_handler_lib",
    hdrs = ["fatal_error_handler.h"],
)

envoy_cc_library(
    name = "sigaction_lib",
    srcs = ["signal_action.cc"],
    hdrs = ["signal_action.h"],
    tags = ["backtrace"],
    deps = [
        ":fatal_error_handler_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:non_copyable",
        "//source/common/singleton:threadsafe_singleton",
        "//source/server:backtrace_lib",
    ],
)
