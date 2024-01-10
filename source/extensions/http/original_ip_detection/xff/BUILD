load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "xff_lib",
    srcs = ["xff.cc"],
    hdrs = ["xff.h"],
    # This extension is core code.
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/http:original_ip_detection_interface",
        "//source/common/http:utility_lib",
        "//source/common/network:cidr_range_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/http/original_ip_detection/xff/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    # This extension is core code.
    visibility = ["//visibility:public"],
    deps = [
        ":xff_lib",
        "//envoy/http:original_ip_detection_interface",
        "//envoy/registry",
        "//source/common/config:utility_lib",
        "//source/common/network:cidr_range_lib",
        "@envoy_api//envoy/extensions/http/original_ip_detection/xff/v3:pkg_cc_proto",
    ],
)
