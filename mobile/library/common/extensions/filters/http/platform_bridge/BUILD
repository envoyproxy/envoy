load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_proto_library(
    name = "filter",
    srcs = ["filter.proto"],
)

envoy_cc_extension(
    name = "platform_bridge_filter_lib",
    srcs = [
        "c_type_definitions.h",
        "filter.cc",
    ],
    hdrs = [
        "c_types.h",
        "filter.h",
    ],
    repository = "@envoy",
    deps = [
        ":filter_cc_proto",
        "//library/common/api:external_api_lib",
        "//library/common/bridge:utility_lib",
        "//library/common/buffer:bridge_fragment_lib",
        "//library/common/http:header_utility_lib",
        "//library/common/stream_info:extra_stream_info_lib",
        "//library/common/types:c_types_lib",
        "@envoy//envoy/common:scope_tracker_interface",
        "@envoy//envoy/http:filter_interface",
        "@envoy//envoy/stats:stats_interface",
        "@envoy//envoy/stats:stats_macros",
        "@envoy//envoy/stats:timespan_interface",
        "@envoy//source/common/common:minimal_logger_lib",
        "@envoy//source/common/common:scope_tracker",
        "@envoy//source/common/stats:timespan_lib",
        "@envoy//source/extensions/filters/http/common:factory_base_lib",
        "@envoy//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    repository = "@envoy",
    deps = [
        ":platform_bridge_filter_lib",
        "@envoy//source/extensions/filters/http/common:factory_base_lib",
    ],
)
