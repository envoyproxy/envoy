load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "data_lib",
    hdrs = ["data.h"],
    deps = [
        "//envoy/http:header_map_interface",
        "//envoy/network:connection_interface",
        "//envoy/stream_info:stream_info_interface",
    ],
)

envoy_cc_library(
    name = "data_impl_lib",
    hdrs = ["data_impl.h"],
    deps = [
        ":data_lib",
    ],
)

envoy_cc_library(
    name = "inputs_lib",
    srcs = ["inputs.cc"],
    hdrs = ["inputs.h"],
    deps = [
        ":data_lib",
        "//source/common/http/matching:inputs_lib",
        "//source/common/network/matching:inputs_lib",
    ],
)
