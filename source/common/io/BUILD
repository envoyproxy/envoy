load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "io_uring_impl_lib",
    srcs = select({
        "//bazel:liburing_enabled": ["io_uring_impl.cc"],
        "//conditions:default": [],
    }),
    hdrs = select({
        "//bazel:liburing_enabled": ["io_uring_impl.h"],
        "//conditions:default": [],
    }),
    tags = ["nocompdb"],
    deps = [
        "//envoy/common/io:io_uring_interface",
        "//envoy/thread_local:thread_local_interface",
    ] + select({
        "//bazel:liburing_enabled": ["//bazel/foreign_cc:liburing_linux"],
        "//conditions:default": [],
    }),
)

envoy_cc_library(
    name = "io_uring_worker_lib",
    srcs = select({
        "//bazel:liburing_enabled": ["io_uring_worker_impl.cc"],
        "//conditions:default": [],
    }),
    hdrs = select({
        "//bazel:liburing_enabled": ["io_uring_worker_impl.h"],
        "//conditions:default": [],
    }),
    deps = [
        ":io_uring_impl_lib",
        "//envoy/common/io:io_uring_interface",
        "//envoy/event:file_event_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:linked_object",
    ],
)

envoy_cc_library(
    name = "io_uring_worker_factory_impl_lib",
    srcs = select({
        "//bazel:liburing_enabled": ["io_uring_worker_factory_impl.cc"],
        "//conditions:default": [],
    }),
    hdrs = select({
        "//bazel:liburing_enabled": ["io_uring_worker_factory_impl.h"],
        "//conditions:default": [],
    }),
    deps = [
        ":io_uring_worker_lib",
        "//envoy/common/io:io_uring_interface",
        "//envoy/thread_local:thread_local_interface",
    ],
)
