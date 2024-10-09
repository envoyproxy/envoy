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
    rbe_pool = "2core",
    # Posix signal tests are irrelevant to Windows
    tags = [
        "backtrace",
        "skip_on_windows",
    ],
    deps = [
        "//source/common/signal:fatal_error_handler_lib",
        "//source/common/signal:sigaction_lib",
        "//test/common/memory:memory_test_utility_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "fatal_action_test",
    srcs = ["fatal_action_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/signal:fatal_error_handler_lib",
        "//test/mocks/server:instance_mocks",
        "//test/test_common:utility_lib",
    ],
)
