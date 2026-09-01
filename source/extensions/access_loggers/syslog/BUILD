load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "syslog_access_log_lib",
    srcs = [
        "formatter.cc",
        "syslog_access_log_impl.cc",
        "udp_sender.cc",
    ],
    hdrs = [
        "formatter.h",
        "sender.h",
        "syslog_access_log_impl.h",
        "udp_sender.h",
    ],
    deps = [
        "//envoy/access_log:access_log_interface",
        "//envoy/common:pure_lib",
        "//envoy/network:address_interface",
        "//envoy/network:socket_interface",
        "//envoy/network:udp_packet_writer_handler_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:utility_lib",
        "//source/common/formatter:substitution_format_utility_lib",
        "//source/common/network:io_socket_error_lib",
        "//source/common/network:socket_lib",
        "//source/common/network:udp_packet_writer_handler_lib",
        "//source/common/stats:symbol_table_lib",
        "//source/extensions/access_loggers/common:access_log_base",
        "@abseil-cpp//absl/strings",
        "@abseil-cpp//absl/strings:str_format",
        "@abseil-cpp//absl/strings:string_view",
        "@envoy_api//envoy/extensions/access_loggers/syslog/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":syslog_access_log_lib",
        "//envoy/access_log:access_log_config_interface",
        "//envoy/common:exception_lib",
        "//envoy/registry",
        "//source/common/config:config_provider_lib",
        "//source/common/formatter:substitution_format_string_lib",
        "//source/common/formatter:substitution_format_utility_lib",
        "//source/common/network:address_lib",
        "//source/common/network:resolver_lib",
        "//source/common/protobuf",
        "@abseil-cpp//absl/status",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/syslog/v3:pkg_cc_proto",
    ],
)
