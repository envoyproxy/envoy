licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "access_log_manager_lib",
    srcs = ["access_log_manager_impl.cc"],
    hdrs = ["access_log_manager_impl.h"],
    deps = [
        "//include/envoy/access_log:access_log_interface",
        "//include/envoy/api:api_interface",
    ],
)
