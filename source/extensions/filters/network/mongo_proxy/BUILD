licenses(["notice"])  # Apache 2

# Mongo proxy L4 network filter (observability and fault injection).
# Public docs: docs/root/configuration/network_filters/mongo_proxy_filter.rst

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "bson_interface",
    hdrs = ["bson.h"],
    deps = ["//include/envoy/buffer:buffer_interface"],
)

envoy_cc_library(
    name = "codec_interface",
    hdrs = ["codec.h"],
    deps = [":bson_interface"],
)

envoy_cc_library(
    name = "bson_lib",
    srcs = ["bson_impl.cc"],
    hdrs = ["bson_impl.h"],
    deps = [
        ":bson_interface",
        "//include/envoy/buffer:buffer_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:byte_order_lib",
        "//source/common/common:hex_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:utility_lib",
    ],
)

envoy_cc_library(
    name = "codec_lib",
    srcs = ["codec_impl.cc"],
    hdrs = ["codec_impl.h"],
    deps = [
        ":bson_lib",
        ":codec_interface",
        "//include/envoy/buffer:buffer_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_library(
    name = "proxy_lib",
    srcs = ["proxy.cc"],
    hdrs = ["proxy.h"],
    deps = [
        ":codec_interface",
        ":codec_lib",
        ":mongo_stats_lib",
        ":utility_lib",
        "//include/envoy/access_log:access_log_interface",
        "//include/envoy/common:time_interface",
        "//include/envoy/filesystem:filesystem_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:drain_decision_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:stats_macros",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/network:filter_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/singleton:const_singleton",
        "//source/extensions/filters/common/fault:fault_config_lib",
        "//source/extensions/filters/network:well_known_names",
    ],
)

envoy_cc_library(
    name = "mongo_stats_lib",
    srcs = ["mongo_stats.cc"],
    hdrs = ["mongo_stats.h"],
    deps = [
        "//include/envoy/stats:stats_interface",
        "//source/common/stats:symbol_table_lib",
    ],
)

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    deps = [
        ":codec_interface",
        "//source/common/json:json_loader_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "requires_trusted_downstream_and_upstream",
    deps = [
        ":proxy_lib",
        "//include/envoy/registry",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/network/mongo_proxy/v3:pkg_cc_proto",
    ],
)
