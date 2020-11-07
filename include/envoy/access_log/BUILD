load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "access_log_interface",
    hdrs = ["access_log.h"],
    deps = [
        "//include/envoy/config:typed_config_interface",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/stream_info:stream_info_interface",
        "//source/common/protobuf",
    ],
)
