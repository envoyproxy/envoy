load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "io_uring_impl_test",
    srcs = ["io_uring_impl_test.cc"],
    tags = [
        "nocompdb",
        "skip_on_windows",
    ],
    deps = [
        "//source/common/io:io_uring_impl_lib",
        "//test/mocks/server:server_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)
