load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "signals_test",
    srcs = ["signals_test.cc"],
    # Posix signal tests are irrelevant to Windows
    tags = [
        "backtrace",
        "skip_on_windows",
    ],
    deps = [
        "//source/common/signal:sigaction_lib",
        "//test/test_common:utility_lib",
    ],
)
