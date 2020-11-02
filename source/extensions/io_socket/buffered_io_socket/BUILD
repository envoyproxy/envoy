load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "peer_buffer_lib",
    hdrs = ["peer_buffer.h"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/buffer:watermark_buffer_lib",
        "//source/common/common:empty_string",
    ],
)

envoy_cc_library(
    name = "buffered_io_socket_handle_lib",
    srcs = [
        "buffered_io_socket_handle_impl.cc",
        "user_space_file_event_impl.cc",
    ],
    hdrs = [
        "buffered_io_socket_handle_impl.h",
        "user_space_file_event_impl.h",
    ],
    deps = [
        ":peer_buffer_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/network:default_socket_interface_lib",
    ],
)
