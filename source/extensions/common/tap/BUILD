licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "tap_interface",
    hdrs = ["tap.h"],
)

envoy_cc_library(
    name = "tap_config_base",
    hdrs = ["tap_config_base.h"],
)

envoy_cc_library(
    name = "admin",
    srcs = ["admin.cc"],
    hdrs = ["admin.h"],
    deps = [
        ":tap_interface",
        "//include/envoy/server:admin_interface",
        "//include/envoy/singleton:manager_interface",
        "@envoy_api//envoy/admin/v2alpha:tap_cc",
    ],
)
