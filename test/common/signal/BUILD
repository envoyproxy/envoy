licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "signals_test",
    srcs = ["signals_test.cc"],
    tags = ["backtrace"],
    deps = [
        "//source/common/signal:sigaction_lib",
        "//test/test_common:utility_lib",
    ],
)
