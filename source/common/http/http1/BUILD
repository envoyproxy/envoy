load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "header_formatter_lib",
    srcs = ["header_formatter.cc"],
    hdrs = ["header_formatter.h"],
    deps = [
        "//envoy/http:header_formatter_interface",
    ],
)

envoy_cc_library(
    name = "codec_stats_lib",
    hdrs = ["codec_stats.h"],
    deps = [
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "//source/common/common:thread_lib",
    ],
)

envoy_cc_library(
    name = "codec_lib",
    srcs = ["codec_impl.cc"],
    hdrs = ["codec_impl.h"],
    deps = [
        ":balsa_parser_lib",
        ":codec_stats_lib",
        ":header_formatter_lib",
        ":legacy_parser_lib",
        ":parser_interface",
        "//envoy/buffer:buffer_interface",
        "//envoy/common:scope_tracker_interface",
        "//envoy/http:codec_interface",
        "//envoy/http:header_map_interface",
        "//envoy/network:connection_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/buffer:watermark_buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:cleanup_lib",
        "//source/common/common:dump_state_utils",
        "//source/common/common:statusor_lib",
        "//source/common/common:utility_lib",
        "//source/common/grpc:common_lib",
        "//source/common/http:codec_helper_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:exception_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:status_lib",
        "//source/common/http:utility_lib",
        "//source/common/runtime:runtime_features_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "conn_pool_lib",
    srcs = ["conn_pool.cc"],
    hdrs = ["conn_pool.h"],
    external_deps = ["abseil_optional"],
    deps = [
        "//envoy/event:deferred_deletable",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/http:conn_pool_interface",
        "//envoy/http:header_map_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/http:codec_client_lib",
        "//source/common/http:codec_wrappers_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:conn_pool_base_lib",
        "//source/common/http:headers_lib",
        "//source/common/runtime:runtime_features_lib",
        "//source/common/upstream:upstream_lib",
    ],
)

envoy_cc_library(
    name = "settings_lib",
    srcs = ["settings.cc"],
    hdrs = ["settings.h"],
    external_deps = ["abseil_optional"],
    deps = [
        "//envoy/http:codec_interface",
        "//envoy/protobuf:message_validator_interface",
        "//source/common/config:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "parser_interface",
    hdrs = ["parser.h"],
    deps = [
        "//source/common/common:statusor_lib",
        "//source/common/http:status_lib",
    ],
)

envoy_cc_library(
    name = "legacy_parser_lib",
    srcs = ["legacy_parser_impl.cc"],
    hdrs = ["legacy_parser_impl.h"],
    external_deps = ["http_parser"],
    deps = [
        ":parser_interface",
        "//source/common/common:assert_lib",
    ],
)

envoy_cc_library(
    name = "balsa_parser_lib",
    srcs = ["balsa_parser.cc"],
    hdrs = ["balsa_parser.h"],
    deps = [
        ":parser_interface",
        "//source/common/common:assert_lib",
        "//source/common/http:headers_lib",
        "@com_github_google_quiche//:quiche_balsa_balsa_enums_lib",
        "@com_github_google_quiche//:quiche_balsa_balsa_frame_lib",
        "@com_github_google_quiche//:quiche_balsa_balsa_headers_lib",
        "@com_github_google_quiche//:quiche_balsa_balsa_visitor_interface_lib",
    ],
)
