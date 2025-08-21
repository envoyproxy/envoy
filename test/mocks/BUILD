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
        "//envoy/common:conn_pool_interface",
        "//envoy/common:key_value_store_interface",
        "//envoy/common:random_generator_interface",
        "//envoy/common:time_interface",
        "//source/common/common:minimal_logger_lib",
        "//test/test_common:test_time_lib",
    ],
)
