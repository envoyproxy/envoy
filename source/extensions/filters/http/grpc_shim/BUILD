licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "grpc_shim_lib",
    srcs = ["grpc_shim.cc"],
    hdrs = ["grpc_shim.h"],
    deps = [
        "//include/envoy/http:filter_interface",
        "//source/common/common:enum_to_int",
        "//source/common/grpc:codec_lib",
        "//source/common/grpc:common_lib",
        "//source/common/grpc:status_lib",
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":grpc_shim_lib",
        "//include/envoy/http:filter_interface",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/config/filter/http/grpc_shim/v2alpha1:grpc_shim_cc",
    ],
)
