load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "custom_header_lib",
    srcs = ["custom_header.cc"],
    hdrs = ["custom_header.h"],
    # This extension is used from core tests.
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/http:original_ip_detection_interface",
        "//source/common/network:utility_lib",
        "@envoy_api//envoy/extensions/http/original_ip_detection/custom_header/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    # This extension is used from core tests.
    visibility = ["//visibility:public"],
    deps = [
        ":custom_header_lib",
        "//envoy/http:original_ip_detection_interface",
        "//envoy/registry",
        "//source/common/config:utility_lib",
        "@envoy_api//envoy/extensions/http/original_ip_detection/custom_header/v3:pkg_cc_proto",
    ],
)
