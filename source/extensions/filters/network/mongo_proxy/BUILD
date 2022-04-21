load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Mongo proxy L4 network filter (observability and fault injection).
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/listeners/network_filters/mongo_proxy_filter

envoy_extension_package()

envoy_cc_library(
    name = "bson_interface",
    hdrs = ["bson.h"],
    deps = ["//envoy/buffer:buffer_interface"],
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
        "//envoy/buffer:buffer_interface",
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
        "//envoy/buffer:buffer_interface",
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
        "//envoy/access_log:access_log_interface",
        "//envoy/common:time_interface",
        "//envoy/filesystem:filesystem_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:drain_decision_interface",
        "//envoy/network:filter_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
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
        "//envoy/stats:stats_interface",
        "//source/common/stats:symbol_table_lib",
        "//source/common/stats:utility_lib",
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
    deps = [
        ":proxy_lib",
        "//envoy/registry",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/network/mongo_proxy/v3:pkg_cc_proto",
    ],
)
