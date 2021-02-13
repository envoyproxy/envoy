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
    # This extensions is core code.
    visibility = ["//visibility:public"],
    deps = [
        "//include/envoy/http:original_ip_detection_interface",
        "//source/common/http:utility_lib",
        "@envoy_api//envoy/extensions/original_ip_detection/xff/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    category = "envoy.original_ip_detection",
    security_posture = "robust_to_untrusted_downstream",
    deps = [
        ":xff_lib",
        "//include/envoy/http:original_ip_detection_interface",
        "//include/envoy/registry",
        "@envoy_api//envoy/extensions/original_ip_detection/xff/v3:pkg_cc_proto",
    ],
)
