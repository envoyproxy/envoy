load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# MySQL proxy L7 network filter.
# Public docs: docs/root/configuration/network_filters/mysql_proxy_filter.rst

envoy_extension_package()

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
        "//include/envoy/network:filter_interface",
        "//include/envoy/server:filter_config_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:stats_macros",
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
        "//source/common/buffer:buffer_lib",
        "//source/extensions/common/sqlutils:sqlutils_lib",
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
        "@envoy_api//envoy/extensions/filters/network/mysql_proxy/v3:pkg_cc_proto",
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

envoy_cc_extension(
    name = "config",
    srcs = ["mysql_config.cc"],
    hdrs = ["mysql_config.h"],
    category = "envoy.filters.network",
    security_posture = "requires_trusted_downstream_and_upstream",
    status = "alpha",
    deps = [
        ":filter_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/network/mysql_proxy/v3:pkg_cc_proto",
    ],
)
