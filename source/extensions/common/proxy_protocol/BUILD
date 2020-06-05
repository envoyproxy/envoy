load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "proxy_protocol_header_lib",
    srcs = ["proxy_protocol_header.cc"],
    hdrs = ["proxy_protocol_header.h"],
    deps = [
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/network:address_interface",
        "//include/envoy/network:connection_interface",
        "//source/common/network:address_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
