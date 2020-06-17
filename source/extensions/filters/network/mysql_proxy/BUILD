load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

# MySQL proxy L7 network filter.
# Public docs: docs/root/configuration/network_filters/mysql_proxy_filter.rst

envoy_package()

envoy_cc_library(
    name = "proxy_lib",
    srcs = [
        "mysql_codec_clogin.cc",
        "mysql_codec_clogin_resp.cc",
        "mysql_codec_command.cc",
        "mysql_codec_greeting.cc",
        "mysql_codec_switch_resp.cc",
        "mysql_decoder.cc",
        "mysql_filter.cc",
        "mysql_utils.cc",
    ],
    hdrs = [
        "mysql_codec.h",
        "mysql_codec_clogin.h",
        "mysql_codec_clogin_resp.h",
        "mysql_codec_command.h",
        "mysql_codec_greeting.h",
        "mysql_codec_switch_resp.h",
        "mysql_decoder.h",
        "mysql_filter.h",
        "mysql_session.h",
        "mysql_utils.h",
    ],
    deps = [
        "//include/envoy/network:filter_interface",
        "//include/envoy/server:filter_config_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:stats_macros",
        "//source/common/buffer:buffer_lib",
        "//source/common/network:filter_lib",
        "//source/extensions/common/sqlutils:sqlutils_lib",
        "//source/extensions/filters/network:well_known_names",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["mysql_config.cc"],
    hdrs = ["mysql_config.h"],
    security_posture = "requires_trusted_downstream_and_upstream",
    status = "alpha",
    deps = [
        ":proxy_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/network/mysql_proxy/v3:pkg_cc_proto",
    ],
)
