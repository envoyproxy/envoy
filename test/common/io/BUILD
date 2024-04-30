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
        "//source/common/network:address_lib",
        "//test/mocks/io:io_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "io_uring_worker_impl_test",
    srcs = select({
        "//bazel:linux": ["io_uring_worker_impl_test.cc"],
        "//conditions:default": [],
    }),
    deps = [
        "//test/mocks/event:event_mocks",
        "//test/mocks/io:io_mocks",
        "//test/test_common:utility_lib",
    ] + select({
        "//bazel:linux": [
            "//source/common/io:io_uring_worker_lib",
        ],
        "//conditions:default": [],
    }),
)

envoy_cc_test(
    name = "io_uring_worker_impl_integration_test",
    srcs = select({
        "//bazel:linux": ["io_uring_worker_impl_integration_test.cc"],
        "//conditions:default": [],
    }),
    deps = [
        "//envoy/api:os_sys_calls_interface",
        "//source/common/api:os_sys_calls_lib",
        "//test/test_common:test_time_lib",
    ] + select({
        "//bazel:linux": [
            "//source/common/io:io_uring_worker_lib",
        ],
        "//conditions:default": [],
    }),
)
