load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "ratelimit_interface",
    hdrs = ["ratelimit.h"],
    deps = [
        "//include/envoy/common:time_interface",
        "//include/envoy/config:typed_config_interface",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/protobuf:message_validator_interface",
        "//include/envoy/stream_info:stream_info_interface",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)
