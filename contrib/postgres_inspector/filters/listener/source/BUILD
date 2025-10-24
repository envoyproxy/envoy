load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

# PostgreSQL inspector listener filter.
# Detects PostgreSQL protocol and extracts connection metadata for routing.

envoy_cc_library(
    name = "postgres_message_parser_lib",
    srcs = ["postgres_message_parser.cc"],
    hdrs = ["postgres_message_parser.h"],
    repository = "@envoy",
    deps = [
        "//contrib/postgres/protocol:postgres_protocol_lib",
        "//envoy/buffer:buffer_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
    ],
)

envoy_cc_library(
    name = "postgres_inspector_lib",
    srcs = ["postgres_inspector.cc"],
    hdrs = ["postgres_inspector.h"],
    repository = "@envoy",
    deps = [
        ":postgres_message_parser_lib",
        "//envoy/event:timer_interface",
        "//envoy/network:filter_interface",
        "//envoy/network:listen_socket_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/network:address_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//contrib/envoy/extensions/filters/listener/postgres_inspector/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    repository = "@envoy",
    deps = [
        ":postgres_inspector_lib",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/common/protobuf:message_validator_lib",
        "@envoy_api//contrib/envoy/extensions/filters/listener/postgres_inspector/v3alpha:pkg_cc_proto",
    ],
)
