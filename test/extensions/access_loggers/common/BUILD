load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "access_log_base_test",
    srcs = ["access_log_base_test.cc"],
    deps = [
        "//source/extensions/access_loggers/common:access_log_base",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
    ],
)
