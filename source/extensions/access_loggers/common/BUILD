licenses(["notice"])  # Apache 2

# Base class for implementations of AccessLog::Instance.

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "access_log_base",
    srcs = ["access_log_base.cc"],
    hdrs = ["access_log_base.h"],
    deps = [
        "//include/envoy/access_log:access_log_interface",
        "//source/common/access_log:access_log_lib",
        "//source/common/http:header_map_lib",
        "//source/common/singleton:const_singleton",
    ],
)
