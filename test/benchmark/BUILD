licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)

envoy_package()

envoy_cc_test_library(
    name = "main",
    srcs = ["main.cc"],
    external_deps = [
        "abseil_symbolize",
        "benchmark",
    ],
    deps = select({
        "//bazel:disable_signal_trace": [],
        "//conditions:default": ["//source/common/signal:sigaction_lib"],
    }),
)
