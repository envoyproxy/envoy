licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)

envoy_package()

# TODO(htuch): remove when we have a solution for https://github.com/bazelbuild/bazel/issues/3510
envoy_cc_test_library(
    name = "dummy_main",
    srcs = ["dummy_main.cc"],
)

envoy_cc_test_library(
    name = "main",
    srcs = [
        "main.cc",
        "test_runner.h",
    ],
    deps = [
        "//source/common/common:logger_lib",
        "//source/common/common:thread_lib",
        "//source/common/event:libevent_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:printers_lib",
    ] + select({
        "//bazel:disable_signal_trace": [],
        "//conditions:default": ["//source/exe:sigaction_lib"],
    }),
)
