load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

#package(default_visibility = ["//visibility:public"])

# SMTP proxy L7 network filter.
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/listeners/network_filters/smtp_proxy_filter

envoy_cc_library(
    name = "filter",
    srcs = [
        "smtp_filter.cc",
    ],
    hdrs = [
        "smtp_filter.h",
    ],
    repository = "@envoy",
    deps = [
        "smtp_decoder_lib",
        "//envoy/access_log:access_log_interface",
        "//envoy/network:filter_interface",
        "//envoy/server:filter_config_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "//source/common/buffer:buffer_lib",
        "//source/common/network:filter_lib",
        "//source/extensions/filters/network:well_known_names",
        "@envoy_api//contrib/envoy/extensions/filters/network/smtp_proxy/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "smtp_decoder_lib",
    srcs = ["smtp_decoder_impl.cc"],
    hdrs = [
        "smtp_decoder.h",
        "smtp_decoder_impl.h",
    ],
    deps = [
        "smtp_session_lib",
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/network:well_known_names",
    ],
)

envoy_cc_library(
    name = "smtp_session_lib",
    srcs = ["smtp_session.cc"],
    hdrs = [
        "smtp_command.h",
        "smtp_decoder.h",
        "smtp_handler.h",
        "smtp_session.h",
    ],
    deps = [
        "smtp_transaction_lib",
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/network:well_known_names",
    ],
)

envoy_cc_library(
    name = "smtp_transaction_lib",
    srcs = ["smtp_transaction.cc"],
    hdrs = [
        "smtp_command.h",
        "smtp_decoder.h",
        "smtp_transaction.h",
    ],
    deps = [
        "smtp_utils_lib",
        "//envoy/stream_info:stream_info_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/filters/network:well_known_names",
    ],
)

envoy_cc_library(
    name = "smtp_utils_lib",
    srcs = ["smtp_utils.cc"],
    hdrs = ["smtp_utils.h"],
    deps = [],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    repository = "@envoy",
    deps = [
        ":filter",
        "//envoy/access_log:access_log_interface",
        "//source/common/access_log:access_log_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/smtp_proxy/v3alpha:pkg_cc_proto",
        "@envoy_api//envoy/config/accesslog/v3:pkg_cc_proto",
    ],
)
