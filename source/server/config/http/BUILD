licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "fault_lib",
    srcs = ["fault.cc"],
    hdrs = ["fault.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:filter_json_lib",
        "//source/common/config:well_known_names",
        "//source/common/http/filter:fault_filter_lib",
    ],
)

envoy_cc_library(
    name = "gzip_lib",
    srcs = ["gzip.cc"],
    hdrs = ["gzip.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:well_known_names",
        "//source/common/http/filter:gzip_filter_lib",
        "//source/common/protobuf:utility_lib",
    ],
)
