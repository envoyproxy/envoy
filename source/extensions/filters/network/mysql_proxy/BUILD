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
        ":client_lib",
        ":codec_lib",
        ":conn_pool_lib",
        ":decoder_lib",
        ":message_helper_lib",
        ":new_conn_pool_lib",
        ":route_lib",
        "//include/envoy/network:filter_interface",
        "//include/envoy/server:filter_config_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:stats_macros",
        "//source/common/network:filter_lib",
        "//source/extensions/filters/network:well_known_names",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/mysql_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "message_helper_lib",
    srcs = ["message_helper.cc"],
    hdrs = ["message_helper.h"],
    deps = [
        ":codec_lib",
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
        "//include/envoy/upstream:cluster_manager_interface",
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
        ":conn_pool_interface",
        "//include/envoy/upstream:cluster_manager_interface",
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
        ":conn_pool_manager_lib",
        ":route_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "@envoy_api//envoy/extensions/filters/network/mysql_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "conn_pool_interface",
    hdrs = [
        "conn_pool.h",
    ],
    deps = [
        ":decoder_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "@envoy_api//envoy/extensions/filters/network/mysql_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "conn_pool_manager_interface",
    hdrs = [
        "conn_pool_manager.h",
    ],
    deps = [
        ":new_conn_pool_lib",
    ],
)

envoy_cc_library(
    name = "conn_pool_manager_lib",
    srcs = [
        "conn_pool_manager_impl.cc",
    ],
    hdrs = [
        "conn_pool_manager_impl.h",
    ],
    deps = [
        ":conn_pool_manager_interface",
    ],
)

envoy_cc_library(
    name = "new_conn_pool_interface",
    hdrs = [
        "new_conn_pool.h",
    ],
    deps = [
        "//include/envoy/upstream:cluster_manager_interface",
        "@envoy_api//envoy/extensions/filters/network/mysql_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "new_conn_pool_lib",
    srcs = [
        "new_conn_pool_impl.cc",
    ],
    hdrs = [
        "new_conn_pool_impl.h",
    ],
    deps = [
        ":client_interface",
        ":codec_lib",
        ":decoder_lib",
        ":message_helper_lib",
        ":new_conn_pool_interface",
        "//include/envoy/event:deferred_deletable",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/common:linked_object",
        "//source/common/common:minimal_logger_lib",
        "//source/common/conn_pool:conn_pool_base_lib",
        "@envoy_api//envoy/extensions/filters/network/mysql_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "conn_pool_lib",
    srcs = [
        "conn_pool_impl.cc",
    ],
    hdrs = [
        "conn_pool_impl.h",
    ],
    deps = [
        ":client_interface",
        ":codec_lib",
        ":conn_pool_interface",
        ":decoder_lib",
        ":message_helper_lib",
        "//include/envoy/event:deferred_deletable",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/common:linked_object",
        "//source/common/common:minimal_logger_lib",
        "@envoy_api//envoy/extensions/filters/network/mysql_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "client_interface",
    hdrs = ["mysql_client.h"],
    deps = [
        ":conn_pool_interface",
        ":decoder_interface",
        "//include/envoy/upstream:cluster_manager_interface",
    ],
)

envoy_cc_library(
    name = "client_lib",
    srcs = ["mysql_client_impl.cc"],
    hdrs = ["mysql_client_impl.h"],
    deps = [
        ":client_interface",
        ":decoder_lib",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/network:filter_lib",
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
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/mysql_proxy/v3:pkg_cc_proto",
    ],
)
