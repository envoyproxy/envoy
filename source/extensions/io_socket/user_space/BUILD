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
    # for integration tests and bootstrap.internal_listener
    visibility = ["//visibility:public"],
    deps = [
        ":io_handle_impl_lib",
    ],
)

envoy_cc_library(
    name = "io_handle_lib",
    hdrs = ["io_handle.h"],
    deps = [
        "//envoy/stream_info:filter_state_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/buffer:watermark_buffer_lib",
        "//source/common/common:empty_string",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
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
        "//envoy/event:dispatcher_interface",
    ],
)

envoy_cc_library(
    name = "io_handle_impl_lib",
    srcs = [
        "io_handle_impl.cc",
    ],
    hdrs = [
        "io_handle_impl.h",
    ],
    deps = [
        ":file_event_lib",
        ":io_handle_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/network:address_lib",
        "//source/common/network:default_socket_interface_lib",
    ],
)
