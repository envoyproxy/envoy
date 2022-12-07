load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "io_uring_interface",
    hdrs = [
        "io_uring.h",
    ],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/network:address_lib",
    ],
)

envoy_cc_library(
    name = "io_uring_impl_lib",
    srcs = [
        "io_uring_impl.cc",
    ],
    hdrs = [
        "io_uring_impl.h",
    ],
    external_deps = ["uring"],
    tags = ["nocompdb"],
    deps = [
        "//source/common/common:utility_lib",
        ":io_uring_interface",
    ],
)

envoy_cc_library(
    name = "io_uring_worker_lib",
    srcs = [
        "io_uring_worker.cc"
    ],
    hdrs = [
        "io_uring_worker.h"
    ],
    tags = ["nocompdb"],
    deps = [
        ":io_uring_interface",
        ":io_uring_impl_lib",
        "//source/common/buffer:buffer_lib",
    ],
)

envoy_cc_library(
    name = "io_uring_factory_lib",
    srcs = [
        "io_uring_factory.cc"
    ],
    hdrs = [
        "io_uring_factory.h"
    ],
    tags = ["nocompdb"],
    deps = [
        "//envoy/thread_local:thread_local_interface",
        ":io_uring_interface",
        ":io_uring_worker_lib",
    ],
)
