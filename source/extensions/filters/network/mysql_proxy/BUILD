licenses(["notice"])  # Apache 2

# Mysql proxy L4 network filter (observability and fault injection).
# Public docs: docs/root/configuration/network_filters/mysql_proxy_filter.rst

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "proxy_lib",
    srcs = [
        "mysql_codec.cc",
        "mysql_codec_clogin.cc",
        "mysql_codec_greeting.cc",
        "mysql_codec_loginok.cc",
        "mysql_codec_switch_resp.cc",
        "mysql_filter.cc",
    ],
    hdrs = [
        "mysql_codec.h",
        "mysql_codec_clogin.h",
        "mysql_codec_greeting.h",
        "mysql_codec_loginok.h",
        "mysql_codec_query.h",
        "mysql_codec_srvresp.h",
        "mysql_codec_switch_resp.h",
        "mysql_filter.h",
        "mysql_session.h",
    ],
    deps = [
        "//include/envoy/network:filter_interface",
        "//include/envoy/server:filter_config_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:stats_macros",
        "//source/common/config:filter_json_lib",
        "//source/common/network:filter_lib",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["mysql_config.cc"],
    hdrs = ["mysql_config.h"],
    deps = [
        ":proxy_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//envoy/config/filter/network/mysql_proxy/v2:mysql_proxy_cc",
    ],
)
