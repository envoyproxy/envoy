load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":reverse_tunnel_filter_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/network/reverse_tunnel/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "reverse_tunnel_filter_lib",
    srcs = ["reverse_tunnel_filter.cc"],
    hdrs = ["reverse_tunnel_filter.h"],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//envoy/formatter:substitution_formatter_interface",
        "//envoy/http:codec_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "//envoy/ssl:connection_interface",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:logger_lib",
        "//source/common/formatter:substitution_format_string_lib",
        "//source/common/formatter:substitution_formatter_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/common/http/http1:codec_lib",
        "//source/common/http/http1:codec_stats_lib",
        "//source/common/http/http1:settings_lib",
        "//source/common/network:connection_socket_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:message_validator_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/router:string_accessor_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/bootstrap/reverse_tunnel/common:reverse_connection_utility_lib",
        "//source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface:reverse_tunnel_acceptor_includes",
        "//source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface:reverse_tunnel_acceptor_lib",
        "//source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface:upstream_socket_manager_lib",
        "//source/server:generic_factory_context_lib",
        "//source/server:null_overload_manager_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/reverse_tunnel/v3:pkg_cc_proto",
    ],
)
