load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "custom_response_filter",
    srcs = [
        "custom_response_filter.cc",
    ],
    hdrs = [
        "custom_response_filter.h",
    ],
    deps = [
        ":config",
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
    ],
    hdrs = [
        "config.h",
    ],
    deps = [
        ":policy_interface",
        "//envoy/http:codes_interface",
        "//envoy/server:filter_config_interface",
        "//envoy/stream_info:stream_info_interface",
        "//source/common/http:header_map_lib",
        "//source/common/http/matching:data_impl_lib",
        "//source/common/http/matching:inputs_lib",
        "//source/common/http/matching:status_code_input_lib",
        "//source/common/matcher:matcher_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/custom_response/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "policy_interface",
    hdrs = [
        "policy.h",
    ],
    deps = [
        "//envoy/config:typed_config_interface",
        "//envoy/http:filter_interface",
        "//envoy/http:header_map_interface",
        "//envoy/stream_info:stream_info_interface",
    ],
)

envoy_cc_extension(
    name = "factory",
    srcs = [
        "factory.cc",
    ],
    hdrs = [
        "factory.h",
    ],
    deps = [
        ":config",
        ":custom_response_filter",
        "//envoy/server:factory_context_interface",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/custom_response/v3:pkg_cc_proto",
    ],
)
