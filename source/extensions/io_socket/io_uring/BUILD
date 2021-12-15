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
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":io_handle_impl_lib",
        "//source/common/network:socket_interface_lib",
        "@envoy_api//envoy/extensions/network/socket_interface/v3:pkg_cc_proto",
    ] + select({
        "//bazel:linux": ["//source/common/io:io_uring_impl_lib"],
        "//conditions:default": [],
    }),
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
        "//envoy/event:dispatcher_interface",
        "//envoy/network:io_handle_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/io:io_uring_interface",
        "//source/common/network:address_lib",
        "//source/common/network:io_socket_error_lib",
    ],
)
