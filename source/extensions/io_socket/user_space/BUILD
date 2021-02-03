load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.h"],
    security_posture = "unknown",
    status = "wip",
    undocumented = True,
    deps = [
    ],
)

envoy_cc_library(
    name = "io_handle_lib",
    hdrs = ["io_handle.h"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/buffer:watermark_buffer_lib",
        "//source/common/common:empty_string",
    ],
)

envoy_cc_library(
    name = "file_event_lib",
    srcs = [
        "file_event_impl.cc",
    ],
    hdrs = [
        "file_event_impl.h",
    ],
    deps = [
        ":io_handle_lib",
        "//include/envoy/event:dispatcher_interface",
    ],
)
