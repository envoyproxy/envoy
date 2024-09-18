load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "proxy_lib",
    srcs = [
        "proxy.cc",
    ],
    hdrs = [
        "proxy.h",
    ],
    deps = [
        ":proxy_config_interface",
        ":rds_lib",
        ":route_lib",
        ":stats_lib",
        ":tracing_lib",
        "//envoy/network:filter_interface",
        "//envoy/server:factory_context_interface",
        "//envoy/stats:timespan_interface",
        "//source/common/common:linked_object",
        "//source/common/common:minimal_logger_lib",
        "//source/common/stats:timespan_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/common/tracing:tracer_config_lib",
        "//source/common/tracing:tracer_lib",
        "//source/common/tracing:tracer_manager_lib",
        "//source/extensions/filters/network/common:factory_base_lib",
        "//source/extensions/filters/network/generic_proxy/interface:codec_interface",
        "//source/extensions/filters/network/generic_proxy/router:router_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/generic_proxy/v3:pkg_cc_proto",
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
        ":proxy_lib",
        "//envoy/server:filter_config_interface",
        "//source/common/access_log:access_log_lib",
        "@envoy_api//envoy/extensions/filters/network/generic_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "route_lib",
    srcs = [
        "route_impl.cc",
    ],
    hdrs = [
        "route_impl.h",
    ],
    deps = [
        ":match_lib",
        ":route_interface",
        "//envoy/server:factory_context_interface",
        "//source/common/common:matchers_lib",
        "//source/common/config:metadata_lib",
        "//source/common/config:utility_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/matcher:matcher_lib",
        "//source/extensions/filters/network/generic_proxy/interface:filter_interface",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/generic_proxy/action/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/generic_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "match_lib",
    srcs = [
        "match.cc",
    ],
    hdrs = [
        "match.h",
    ],
    deps = [
        ":match_input_lib",
        "//source/common/matcher:matcher_lib",
        "//source/extensions/filters/network/generic_proxy/interface:stream_interface",
        "@envoy_api//envoy/extensions/filters/network/generic_proxy/matcher/v3:pkg_cc_proto",
    ],
    alwayslink = 1,
)

envoy_cc_library(
    name = "match_input_lib",
    hdrs = [
        "match_input.h",
    ],
    deps = [
        "//envoy/stream_info:stream_info_interface",
        "//source/extensions/filters/network/generic_proxy/interface:stream_interface",
    ],
)

envoy_cc_library(
    name = "rds_interface",
    hdrs = ["rds.h"],
    deps = [
        "//envoy/rds:rds_interface",
        "@envoy_api//envoy/extensions/filters/network/generic_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "rds_lib",
    hdrs = [
        "rds_impl.h",
    ],
    deps = [
        ":rds_interface",
        ":route_lib",
        "//envoy/rds:rds_interface",
        "//source/common/rds:rds_lib",
        "@envoy_api//envoy/extensions/filters/network/generic_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "stats_lib",
    srcs = ["stats.cc"],
    hdrs = ["stats.h"],
    deps = [
        "//envoy/server:factory_context_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "//source/common/stats:symbol_table_lib",
        "//source/common/stream_info:utility_lib",
    ],
)

envoy_cc_library(
    name = "file_access_log_lib",
    hdrs = [
        "file_access_log.h",
    ],
    deps = [
        "//envoy/access_log:access_log_config_interface",
        "//envoy/access_log:access_log_interface",
        "//source/common/common:utility_lib",
        "//source/common/formatter:substitution_format_string_lib",
        "@envoy_api//envoy/extensions/access_loggers/file/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "access_log_lib",
    srcs = [
        "access_log.cc",
    ],
    hdrs = [
        "access_log.h",
    ],
    deps = [
        ":file_access_log_lib",
        "//source/extensions/filters/network/generic_proxy/interface:stream_interface",
    ],
    # Ensure this factory in the source is always linked in.
    alwayslink = 1,
)

envoy_cc_library(
    name = "tracing_lib",
    srcs = [
        "tracing.cc",
    ],
    hdrs = [
        "tracing.h",
    ],
    deps = [
        "//envoy/common:pure_lib",
        "//envoy/tracing:trace_context_interface",
        "//source/extensions/filters/network/generic_proxy/interface:stream_interface",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:optional",
    ],
)

envoy_cc_library(
    name = "codec_callbacks_interface",
    hdrs = [
        "codec_callbacks.h",
    ],
    deps = [
        ":route_interface",
        "//envoy/buffer:buffer_interface",
        "//envoy/network:drain_decision_interface",
    ],
)

envoy_cc_library(
    name = "route_interface",
    hdrs = [
        "route.h",
    ],
    deps = [
        "//envoy/config:typed_metadata_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/network:connection_interface",
        "//envoy/rds:rds_config_interface",
        "//envoy/router:router_interface",
        "//envoy/stream_info:stream_info_interface",
        "//source/extensions/filters/network/generic_proxy:match_input_lib",
        "//source/extensions/filters/network/generic_proxy/interface:stream_interface",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "proxy_config_interface",
    hdrs = [
        "proxy_config.h",
    ],
    deps = [
        ":route_interface",
        "//envoy/tracing:trace_config_interface",
        "//envoy/tracing:tracer_interface",
        "//source/extensions/filters/network/generic_proxy:access_log_lib",
        "//source/extensions/filters/network/generic_proxy:match_input_lib",
        "//source/extensions/filters/network/generic_proxy/interface:codec_interface",
        "//source/extensions/filters/network/generic_proxy/interface:filter_interface",
    ],
)

envoy_cc_library(
    name = "filter_callbacks_interface",
    hdrs = [
        "filter_callbacks.h",
    ],
    deps = [
        ":route_interface",
    ],
)
