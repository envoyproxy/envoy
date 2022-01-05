load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

# MySQL proxy L7 network filter.
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/listeners/network_filters/mysql_proxy_filter

envoy_cc_library(
    name = "filter_lib",
    srcs = [
        "mysql_filter.cc",
    ],
    hdrs = [
        "mysql_filter.h",
    ],
    deps = [
        ":codec_lib",
        ":decoder_lib",
        "//envoy/network:filter_interface",
        "//envoy/server:filter_config_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "//source/common/network:filter_lib",
        "//source/extensions/filters/network:well_known_names",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "codec_lib",
    srcs = [
        "mysql_codec_clogin.cc",
        "mysql_codec_clogin_resp.cc",
        "mysql_codec_command.cc",
        "mysql_codec_greeting.cc",
        "mysql_codec_switch_resp.cc",
    ],
    hdrs = [
        "mysql_codec_clogin.h",
        "mysql_codec_clogin_resp.h",
        "mysql_codec_command.h",
        "mysql_codec_greeting.h",
        "mysql_codec_switch_resp.h",
        "mysql_session.h",
    ],
    deps = [
        ":codec_interface",
        ":util_lib",
        "//contrib/common/sqlutils/source:sqlutils_lib",
        "//source/common/buffer:buffer_lib",
    ],
)

envoy_cc_library(
    name = "decoder_interface",
    hdrs = ["mysql_decoder.h"],
    deps = [
        ":codec_lib",
    ],
)

envoy_cc_library(
    name = "codec_interface",
    hdrs = ["mysql_codec.h"],
    deps = [
        "//source/common/buffer:buffer_lib",
    ],
)

envoy_cc_library(
    name = "util_lib",
    srcs = ["mysql_utils.cc"],
    hdrs = ["mysql_utils.h"],
    deps = [
        ":codec_interface",
        "//source/common/buffer:buffer_lib",
    ],
)

envoy_cc_library(
    name = "config_interface",
    hdrs = ["mysql_config.h"],
    deps = [
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/mysql_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "decoder_lib",
    srcs = ["mysql_decoder_impl.cc"],
    hdrs = ["mysql_decoder_impl.h"],
    deps = [
        ":decoder_interface",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["mysql_config.cc"],
    hdrs = ["mysql_config.h"],
    deps = [
        ":filter_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/mysql_proxy/v3:pkg_cc_proto",
    ],
)
