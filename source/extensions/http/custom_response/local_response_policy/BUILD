load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "local_response_policy_lib",
    srcs = [
        "local_response_factory.cc",
        "local_response_policy.cc",
    ],
    hdrs = [
        "local_response_factory.h",
        "local_response_policy.h",
    ],
    deps = [
        "//envoy/api:api_interface",
        "//envoy/http:codes_interface",
        "//envoy/http:header_map_interface",
        "//envoy/server:filter_config_interface",
        "//envoy/stream_info:stream_info_interface",
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
        "//source/extensions/filters/http/custom_response:custom_response_filter",
        "//source/extensions/filters/http/custom_response:policy_interface",
        "//source/server:generic_factory_context_lib",
        "@envoy_api//envoy/extensions/http/custom_response/local_response_policy/v3:pkg_cc_proto",
    ],
)
