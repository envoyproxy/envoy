load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "fragment_test",
    srcs = ["fragment_test.cc"],
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/filters/http/file_system_buffer:fragment",
        "//test/extensions/common/async_files:mocks",
        "//test/mocks/buffer:buffer_mocks",
        "//test/test_common:status_utility_lib",
    ],
)
