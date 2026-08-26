load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "fatal_error_handler_lib",
    srcs = ["fatal_error_handler.cc"],
    hdrs = ["fatal_error_handler.h"],
    deps = [
        ":fatal_action_lib",
        "//envoy/event:dispatcher_interface",
        "//source/common/common:macros",
    ],
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

envoy_cc_library(
    name = "non_fatal_signal_handler_lib",
    srcs = ["non_fatal_signal_handler.cc"],
    hdrs = ["non_fatal_signal_handler.h"],
    tags = ["backtrace"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:thread_lib",
    ],
)

envoy_cc_library(
    name = "fatal_action_lib",
    hdrs = ["fatal_action.h"],
    deps = [
        "//envoy/server:fatal_action_interface",
        "//envoy/thread:thread_interface",
    ],
)
