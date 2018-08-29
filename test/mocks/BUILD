licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)

envoy_package()

envoy_cc_test_library(
    name = "common_lib",
    srcs = ["common.cc"],
    hdrs = ["common.h"],
    deps = [
        "//include/envoy/common:time_interface",
        "//include/envoy/common:token_bucket_interface",
        "//include/envoy/event:timer_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/event:real_time_system_lib",
    ],
)
