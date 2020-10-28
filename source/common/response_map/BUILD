load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "response_map_lib",
    srcs = ["response_map.cc"],
    hdrs = ["response_map.h"],
    deps = [
        "//include/envoy/api:api_interface",
        "//include/envoy/http:codes_interface",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/server:filter_config_interface",
        "//include/envoy/stream_info:stream_info_interface",
        "//source/common/access_log:access_log_lib",
        "//source/common/common:enum_to_int",
        "//source/common/config:datasource_lib",
        "//source/common/formatter:substitution_format_string_lib",
        "//source/common/formatter:substitution_formatter_lib",
        "//source/common/http:header_map_lib",
        "//source/common/stream_info:stream_info_lib",
        "@envoy_api//envoy/extensions/filters/http/response_map/v3:pkg_cc_proto",
    ],
)
