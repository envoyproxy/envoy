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
    name = "stats_interface",
    hdrs = [
        "stats.h",
    ],
    deps = [
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "@envoy_api//envoy/extensions/filters/network/mysql_proxy/v3:pkg_cc_proto",
    ],
)

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
        ":route_lib",
        ":stats_interface",
        "//envoy/network:filter_interface",
        "//envoy/server:filter_config_interface",
        "//source/common/network:filter_lib",
        "//source/extensions/filters/network:well_known_names",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "terminal_filter_lib",
    srcs = [
        "mysql_terminal_filter.cc",
    ],
    hdrs = [
        "mysql_terminal_filter.h",
    ],
    deps = [
        ":filter_lib",
        ":message_lib",
        ":route_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/mysql_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "message_lib",
    srcs = [
        "mysql_message.cc",
    ],
    hdrs = [
        "mysql_message.h",
    ],
    deps = [
        ":codec_lib",
        ":util_lib",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/config:datasource_lib",
        "//source/extensions/common/sqlutils:sqlutils_lib",
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
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/config:datasource_lib",
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
    external_deps = [
        "ssl",
    ],
    deps = [
        ":codec_interface",
        "//source/common/buffer:buffer_lib",
    ],
)

envoy_cc_library(
    name = "route_interface",
    hdrs = ["route.h"],
    deps = [
        "//envoy/upstream:cluster_manager_interface",
        "@envoy_api//envoy/extensions/filters/network/mysql_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "route_lib",
    srcs = [
        "route_impl.cc",
    ],
    hdrs = [
        "route_impl.h",
    ],
    deps = [
        ":route_interface",
        "//envoy/upstream:cluster_manager_interface",
        "@envoy_api//envoy/extensions/filters/network/mysql_proxy/v3:pkg_cc_proto",
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
    deps = [
        ":filter_lib",
        ":terminal_filter_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/mysql_proxy/v3:pkg_cc_proto",
    ],
)
