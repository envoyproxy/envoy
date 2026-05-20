load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "extracted_external_address_lib",
    srcs = ["extracted_external_address.cc"],
    hdrs = ["extracted_external_address.h"],
    deps = [
        "//envoy/http:original_ip_detection_interface",
        "//source/common/network:utility_lib",
        "@envoy_api//envoy/extensions/http/original_ip_detection/extracted_external_address/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":extracted_external_address_lib",
        "//envoy/http:original_ip_detection_interface",
        "//envoy/registry",
        "//source/common/config:utility_lib",
        "@envoy_api//envoy/extensions/http/original_ip_detection/extracted_external_address/v3:pkg_cc_proto",
    ],
)
