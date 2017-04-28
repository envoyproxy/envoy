licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "buffer_lib",
    srcs = ["buffer_impl.cc"],
    hdrs = ["buffer_impl.h"],
    deps = [
        "//include/envoy/buffer:buffer_interface",
        "//source/common/event:libevent_lib",
    ],
)
