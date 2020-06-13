load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

# VPP Comms Lib (VCL) adaptor.

envoy_contrib_package()

envoy_cc_library(
    name = "vcl_interface_lib",
    srcs = [
        "vcl_event.cc",
        "vcl_interface.cc",
        "vcl_io_handle.cc",
    ],
    hdrs = [
        "vcl_event.h",
        "vcl_interface.h",
        "vcl_io_handle.h",
    ],
    external_deps = ["vpp_vcl"],
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/network:socket_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/event:libevent_scheduler_lib",
        "//source/common/network:address_lib",
        "//source/common/network:io_socket_error_lib",
        "//source/common/network:socket_interface_lib",
        "//source/common/network:socket_lib",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":vcl_interface_lib",
        "@envoy_api//contrib/envoy/extensions/network/socket_interface/vcl/v3alpha:pkg_cc_proto",
    ],
)
