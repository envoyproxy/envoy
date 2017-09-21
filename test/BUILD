licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_cc_test_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "local_gtest",
    external_deps = ["googletest"],
    strip_include_prefix = "googlemock/include",
)

envoy_cc_test_library(
    name = "main",
    srcs = [
        "main.cc",
        "test_runner.h",
    ],
    deps = [
        "//source/common/common:thread_lib",
        "//source/common/event:libevent_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:printers_lib",
    ] + select({
        "//bazel:disable_signal_trace": [],
        "//conditions:default": ["//source/exe:sigaction_lib"],
    }),
)
