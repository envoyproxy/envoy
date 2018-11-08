licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "stream_info_lib",
    hdrs = ["stream_info_impl.h"],
    deps = [
        ":filter_state_lib",
        "//include/envoy/stream_info:stream_info_interface",
        "//source/common/common:assert_lib",
    ],
)

envoy_cc_library(
    name = "filter_state_lib",
    srcs = ["filter_state_impl.cc"],
    hdrs = ["filter_state_impl.h"],
    deps = [
        "//include/envoy/stream_info:filter_state_interface",
    ],
)

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    external_deps = ["abseil_optional"],
    deps = [
        "//include/envoy/common:time_interface",
        "//include/envoy/stream_info:stream_info_interface",
    ],
)
