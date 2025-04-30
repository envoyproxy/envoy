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
        "//bazel:linux": ["io_uring_impl.cc"],
        "//conditions:default": [],
    }),
    hdrs = [
        "io_uring_impl.h",
    ],
    tags = ["nocompdb"],
    deps = [
        "//bazel/foreign_cc:liburing_linux",
        "//envoy/common/io:io_uring_interface",
        "//envoy/thread_local:thread_local_interface",
    ],
)

envoy_cc_library(
    name = "io_uring_worker_lib",
    srcs = select({
        "//bazel:linux": ["io_uring_worker_impl.cc"],
        "//conditions:default": [],
    }),
    hdrs = ["io_uring_worker_impl.h"],
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
    srcs = ["io_uring_worker_factory_impl.cc"],
    hdrs = ["io_uring_worker_factory_impl.h"],
    deps = [
        ":io_uring_worker_lib",
        "//envoy/common/io:io_uring_interface",
        "//envoy/thread_local:thread_local_interface",
    ],
)
