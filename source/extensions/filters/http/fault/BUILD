load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# HTTP L7 filter that injects faults into the request flow
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/fault_filter

envoy_extension_package()

envoy_cc_library(
    name = "fault_filter_lib",
    srcs = ["fault_filter.cc"],
    hdrs = ["fault_filter.h"],
    deps = [
        "//envoy/event:timer_interface",
        "//envoy/http:codes_interface",
        "//envoy/http:filter_interface",
        "//envoy/http:header_map_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "//source/common/buffer:watermark_buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:token_bucket_impl_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "//source/common/stats:utility_lib",
        "//source/extensions/filters/common/fault:fault_config_lib",
        "//source/extensions/filters/http/common:stream_rate_limiter_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/fault/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//envoy/registry",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/fault:fault_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/fault/v3:pkg_cc_proto",
    ],
)
