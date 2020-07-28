load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_extension_library(
    name = "well_known_names",
    hdrs = ["well_known_names.h"],
    # well known names files are public as long as they exist.
    visibility = ["//visibility:public"],
    deps = [
        "//source/common/singleton:const_singleton",
    ],
)
