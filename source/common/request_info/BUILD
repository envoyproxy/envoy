licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "request_info_lib",
    hdrs = ["request_info_impl.h"],
    deps = [
        "//include/envoy/request_info:request_info_interface",
        "//source/common/common:assert_lib",
    ],
)

envoy_cc_library(
    name = "dynamic_metadata_lib",
    srcs = ["dynamic_metadata_impl.cc"],
    hdrs = ["dynamic_metadata_impl.h"],
    deps = [
        "//include/envoy/request_info:dynamic_metadata_interface",
    ],
)

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    external_deps = ["abseil_optional"],
    deps = [
        "//include/envoy/common:time_interface",
        "//include/envoy/request_info:request_info_interface",
    ],
)
