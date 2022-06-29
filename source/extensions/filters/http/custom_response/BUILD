load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "custom_response",
    srcs = [
        "custom_response_filter.cc",
    ],
    hdrs = [
        "custom_response_filter.h",
    ],
    deps = [
        "//source/common/http:headers_lib",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/custom_response/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = [
        "config.cc",
        "response.cc",
    ],
    hdrs = [
        "config.h",
        "response.h",
    ],
    deps = [
        ":custom_response",
        "//envoy/api:api_interface",
        "//envoy/http:codes_interface",
        "//envoy/http:header_map_interface",
        "//envoy/server:filter_config_interface",
        "//envoy/stream_info:stream_info_interface",
        "//source/common/access_log:access_log_lib",
        "//source/common/common:enum_to_int",
        "//source/common/config:datasource_lib",
        "//source/common/formatter:substitution_format_string_lib",
        "//source/common/formatter:substitution_formatter_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http/matching:data_impl_lib",
        "//source/common/http/matching:inputs_lib",
        "//source/common/matcher:matcher_lib",
        "//source/common/router:header_parser_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/custom_response/v3:pkg_cc_proto",
    ],
)
