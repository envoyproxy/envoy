load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "io_uring_impl_test",
    srcs = select({
        "//bazel:linux_x86_64": ["io_uring_impl_test.cc"],
        "//conditions:default": [],
    }),
    tags = [
        "nocompdb",
        "skip_on_windows",
    ],
    deps = [
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ] + select({
        "//bazel:linux": [
            "//source/common/io:io_uring_impl_lib",
            "//test/mocks/server:server_mocks",
        ],
        "//conditions:default": [],
    }),
)
