load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Local BandwidthShare HTTP L7 filter
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/bandwidth_share_filter

envoy_extension_package()

envoy_cc_library(
    name = "fair_token_bucket_impl_lib",
    srcs = ["fair_token_bucket_impl.cc"],
    hdrs = ["fair_token_bucket_impl.h"],
    deps = [
        "//envoy/common:time_interface",
        "//envoy/common:token_bucket_interface",
        "//source/common/common:thread_lib",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "token_bucket_singleton_lib",
    srcs = ["token_bucket_singleton.cc"],
    hdrs = ["token_bucket_singleton.h"],
    deps = [
        ":fair_token_bucket_impl_lib",
        ":stats_lib",
        "//envoy/common:time_interface",
        "//envoy/singleton:instance_interface",
        "//envoy/singleton:manager_interface",
        "//source/common/common:thread_lib",
        "//source/common/runtime:runtime_lib",
    ],
)

envoy_cc_library(
    name = "stats_lib",
    srcs = ["stats.cc"],
    hdrs = ["stats.h"],
    deps = [
        "//envoy/stats:stats_interface",
        "//envoy/stats:tag_interface",
        "//source/common/common:thread_lib",
        "//source/common/stats:symbol_table_lib",
        "//source/common/stats:utility_lib",
    ],
)

envoy_cc_library(
    name = "bandwidth_share_lib",
    srcs = [
        "filter.cc",
        "filter_config.cc",
    ],
    hdrs = [
        "filter.h",
        "filter_config.h",
    ],
    deps = [
        ":fair_token_bucket_impl_lib",
        ":stats_lib",
        ":token_bucket_singleton_lib",
        "//envoy/http:filter_interface",
        "//envoy/matcher:matcher_interface",
        "//envoy/stats:timespan_interface",
        "//source/common/http/matching:data_impl_lib",
        "//source/common/matcher/actions:string_returning_action_lib",
        "//source/common/runtime:runtime_protos_lib",
        "//source/common/stats:timespan_lib",
        "//source/extensions/filters/http/common:stream_rate_limiter_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    deps = [
        ":bandwidth_share_lib",
        "//envoy/http:filter_interface",
        "//source/common/http/matching:data_impl_lib",
        "//source/common/matcher:matcher_lib",
        "//source/common/matcher/actions:string_returning_action_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/bandwidth_share/v3:pkg_cc_proto",
    ],
)
