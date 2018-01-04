licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)

envoy_package()

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

sh_test(
    name = "version_consistency_test",
    srcs = ["version_consistency_test.sh"],
    data = [
        "//:RAW_RELEASE_NOTES.md",
        "@envoy_api//:VERSION",
    ],
)
