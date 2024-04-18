load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "proxy_protocol_header_lib",
    srcs = ["proxy_protocol_header.cc"],
    hdrs = ["proxy_protocol_header.h"],
    # This is used by the router, so considered core code.
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//envoy/network:address_interface",
        "//envoy/network:connection_interface",
        "//source/common/network:address_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
