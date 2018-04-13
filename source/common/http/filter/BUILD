licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "gzip_filter_lib",
    srcs = ["gzip_filter.cc"],
    hdrs = ["gzip_filter.h"],
    deps = [
        "//include/envoy/http:filter_interface",
        "//include/envoy/json:json_object_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/compressor:compressor_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/json:config_schemas_lib",
        "//source/common/json:json_validator_lib",
        "//source/common/protobuf",
        "@envoy_api//envoy/config/filter/http/gzip/v2:gzip_cc",
    ],
)
