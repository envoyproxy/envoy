load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_mock(
    name = "access_log_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//include/envoy/access_log:access_log_interface",
        "//include/envoy/stream_info:stream_info_interface",
        "//test/mocks/filesystem:filesystem_mocks",
    ],
)
