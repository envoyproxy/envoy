load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "utility_lib",
    hdrs = ["utility.h"],
    deps = [
        "//include/envoy/runtime:runtime_interface",
        "//source/common/common:documentation_url_lib",
        "//source/common/common:minimal_logger_lib",
    ],
)
