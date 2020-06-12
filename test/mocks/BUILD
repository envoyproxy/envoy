load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "common_lib",
    srcs = ["common.cc"],
    hdrs = ["common.h"],
    deps = [
        "//include/envoy/common:conn_pool_interface",
        "//include/envoy/common:time_interface",
        "//include/envoy/common:token_bucket_interface",
        "//source/common/common:minimal_logger_lib",
        "//test/test_common:test_time_lib",
    ],
)
