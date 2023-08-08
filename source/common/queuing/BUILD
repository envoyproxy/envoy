load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "queuing_lib",
    hdrs = [
        "adaptive_lifo_queue.h",
        "fifo_queue.h",
        "queue_base.h",
    ],
    deps = [
        "//source/common/common:linked_object",
    ],
)
