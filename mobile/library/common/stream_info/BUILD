load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "extra_stream_info_lib",
    srcs = ["extra_stream_info.cc"],
    hdrs = ["extra_stream_info.h"],
    repository = "@envoy",
    deps = [
        "//library/common/network:connectivity_manager_lib",
        "//library/common/types:c_types_lib",
        "@envoy//envoy/stream_info:stream_info_interface",
        "@envoy//source/common/stream_info:utility_lib",
    ],
)
