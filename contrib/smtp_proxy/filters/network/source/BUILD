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
    srcs = ["smtp_decoder.cc"],
    hdrs = ["smtp_decoder.h", "smtp_session.h", "smtp_transaction.h", "smtp_utils.h"],
    deps = [
        "//source/common/buffer:buffer_lib",
    ],
)



# "@envoy//envoy/registry",
#         "@envoy//source/common/network:connection_lib",
#         "@envoy//source/extensions/filters/network/common:factory_base_lib",
#         "@envoy//source/common/common:minimal_logger_lib",
#         "@envoy//source/common/common:utility_lib",

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    repository = "@envoy",
    deps = [
        ":filter",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/smtp_proxy/v3alpha:pkg_cc_proto",
    ],
)
