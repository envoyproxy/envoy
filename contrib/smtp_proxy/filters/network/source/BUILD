load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

#package(default_visibility = ["//visibility:public"])

# Smtp proxy L7 network filter.
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
        ":decoder",
        "//envoy/network:filter_interface",
        "//envoy/server:filter_config_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "//source/common/buffer:buffer_lib",
        "//source/common/network:filter_lib",
	"//source/extensions/common/proxy_protocol:proxy_protocol_header_lib",
        "//source/extensions/filters/network:well_known_names",
        "@envoy_api//contrib/envoy/extensions/filters/network/smtp_proxy/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "decoder",
    srcs = [
        "smtp_decoder.cc",
    ],
    hdrs = [
        "smtp_decoder.h",
    ],
    repository = "@envoy",
    deps = [
        "//source/common/buffer:buffer_lib",
    ],
)

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
