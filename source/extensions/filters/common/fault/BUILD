licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "fault_config_lib",
    srcs = ["fault_config.cc"],
    hdrs = ["fault_config.h"],
    deps = [
        "//include/envoy/http:header_map_interface",
        "//source/common/http:headers_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/filter/fault/v2:fault_cc",
    ],
)
