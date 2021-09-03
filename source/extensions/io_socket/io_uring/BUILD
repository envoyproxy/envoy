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
        "//bazel:linux": [":io_uring_impl_lib"],
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
        ":io_uring_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/network:io_handle_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/network:address_lib",
        "//source/common/network:io_socket_error_lib",
    ],
)

envoy_cc_library(
    name = "io_uring_interface",
    hdrs = [
        "io_uring.h",
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
    visibility = ["//visibility:private"],
    deps = [
        ":io_uring_interface",
        "//source/common/network:address_lib",
        "//source/common/network:io_socket_error_lib",
    ],
)
