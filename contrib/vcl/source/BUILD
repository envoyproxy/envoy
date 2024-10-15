load("@base_pip3//:requirements.bzl", "requirement")
load("@rules_cc//cc:defs.bzl", "cc_library")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_cmake",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

# VPP Comms Lib (VCL) adaptor.

envoy_contrib_package()

cc_library(
    name = "vpp_vcl",
    srcs = [
        "external/libsvm.a",
        "external/libvlibapi.a",
        "external/libvlibmemoryclient.a",
        "external/libvppcom.a",
        "external/libvppinfra.a",
        "external/vppcom.h",
    ],
    hdrs = ["external/vppcom.h"],
    defines = ["VPP_VCL"],
    includes = ["external/"],
    tags = ["skip_on_windows"],
    visibility = ["//visibility:public"],
)

envoy_cmake(
    name = "build",
    build_data = [requirement("ply")],
    cache_entries = {
        "CMAKE_BUILD_TYPE": "Release",
        "VPP_API_TEST_BUILTIN": "OFF",
        "BUILD_SHARED_LIBS": "OFF",
        "CMAKE_ENABLE_EXPORTS": "OFF",
    },
    copts = ["-Wno-unused-variable"],
    default_cache_entries = {},
    env = {
        "PLYPATHS": "$(locations %s)" % requirement("ply"),
    },
    lib_source = "@com_github_fdio_vpp_vcl//:all",
    linkopts = ["-Wno-unused-variable"],
    out_static_libs = [
        "libvppcom.a",
        "libvppinfra.a",
        "libsvm.a",
        "libvlibapi.a",
        "libvlibmemoryclient.a",
    ],
    postfix_script = """
        mkdir -p $INSTALLDIR/lib/external $INSTALLDIR/include/external \
        && find . -name "*.a" | xargs -I{} cp -a {} $INSTALLDIR/lib/ \
        && find . -name "*.h" ! -name config.h | xargs -I{} cp -a {} $INSTALLDIR/include
    """,
    tags = [
        "cpu:16",
        "skip_on_windows",
    ],
    targets = [
        "vppcom",
    ],
    working_directory = "src",
)

genrule(
    name = "build_files",
    outs = [
        "external/libsvm.a",
        "external/libvlibapi.a",
        "external/libvlibmemoryclient.a",
        "external/libvppcom.a",
        "external/libvppinfra.a",
        "external/vppcom.h",
    ],
    cmd = """
        find . -name "*.a" | grep -v copy_build | xargs -I{} cp -a {} $(RULEDIR)/external \
        && find . -name "vppcom.h" | grep -v copy_build | xargs -I{} cp -a {} $(RULEDIR)/external
    """,
    tools = [":build"],
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
