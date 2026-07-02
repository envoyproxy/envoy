load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "external_buffer_interface",
    hdrs = ["external_buffer.h"],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//envoy/common:pure_lib",
        "//envoy/event:dispatcher_interface",
    ],
)

envoy_cc_library(
    name = "external_buffer_impl_lib",
    srcs = ["external_buffer_impl.cc"],
    hdrs = ["external_buffer_impl.h"],
    deps = [
        ":external_buffer_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
    ],
)

envoy_cc_library(
    name = "buffer_manager_lib",
    srcs = ["buffer_manager.cc"],
    hdrs = ["buffer_manager.h"],
    deps = [
        ":external_buffer_interface",
        "//envoy/http:filter_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:logger_lib",
    ],
)
