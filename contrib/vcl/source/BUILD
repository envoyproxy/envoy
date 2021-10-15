load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)
load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_python//python:defs.bzl", "py_binary")
load("@base_pip3//:requirements.bzl", "requirement")

licenses(["notice"])  # Apache 2

# VPP Comms Lib (VCL) adaptor.

envoy_contrib_package()

cc_library(
    name = "vpp_vcl",
    srcs = [
        "external/libsvm.a",
        "external/libvlibmemoryclient.a",
        "external/libvppcom.a",
        "external/libvppinfra.a",
    ],
    hdrs = ["external/vppcom.h"],
    defines = ["VPP_VCL"],
    includes = ["external/"],
    tags = ["skip_on_windows"],
    visibility = ["//visibility:public"],
)

genrule(
    name = "build",
    srcs = [
        "@com_github_fdio_vpp_vcl//:all",
    ],
    outs = [
        "external/libvppcom.a",
        "external/libvppinfra.a",
        "external/libsvm.a",
        "external/libvlibmemoryclient.a",
        "external/vppcom.h",
    ],
    cmd = """
        ./$(location :vcl_build_launcher) vpp_vcl_build.sh $(location external/libvppcom.a)
    """,
    tools = [
        ":vcl_build_launcher",
    ],
)

py_binary(
    name = "vcl_build_launcher",
    srcs = ["vcl_build_launcher.py"],
    data = [
        "vpp_vcl_build.sh",
    ],
    main = "vcl_build_launcher.py",
    deps = [requirement("ply")],
)

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
    visibility = ["//visibility:public"],
    deps = [
        ":vpp_vcl",
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
    visibility = ["//visibility:public"],
    deps = [
        ":vcl_interface_lib",
        "@envoy_api//contrib/envoy/extensions/vcl/v3alpha:pkg_cc_proto",
    ],
)
