load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_mock(
    name = "local_reply_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = ["//source/common/local_reply:local_reply_lib"],
)
