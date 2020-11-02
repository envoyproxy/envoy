load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

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
        "//source/common/event:dispatcher_includes",
        "//source/common/network:default_socket_interface_lib",
        "//source/common/network:peer_buffer_lib",
    ],
)
