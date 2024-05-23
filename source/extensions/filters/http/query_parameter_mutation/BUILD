load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "filter_lib",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    deps = [
        "//envoy/http:filter_interface",
        "//envoy/server:factory_context_interface",
        "//envoy/server:filter_config_interface",
        "//source/common/config:utility_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//source/extensions/filters/http/query_parameter_mutation:query_params_evaluator_lib",
        "@envoy_api//envoy/extensions/filters/http/query_parameter_mutation/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":filter_lib",
        "//envoy/registry",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/query_parameter_mutation/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "query_params_evaluator_lib",
    srcs = ["query_params_evaluator.cc"],
    hdrs = ["query_params_evaluator.h"],
    deps = [
        "//envoy/formatter:substitution_formatter_interface",
        "//envoy/http:header_map_interface",
        "//envoy/http:query_params_interface",
        "//source/common/formatter:substitution_formatter_lib",
        "//source/common/http:utility_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
