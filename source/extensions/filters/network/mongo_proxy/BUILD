licenses(["notice"])  # Apache 2
# Mongo proxy L4 network filter (observability and fault injection).
# Public docs: https://github.com/envoyproxy/data-plane-api/blob/master/docs/root/configuration/network_filters/mongo_proxy_filter.rst

load(
    "//bazel:envoy_build_system.bzl",
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
        "//source/common/common:logger_lib",
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
        "//source/common/common:logger_lib",
    ],
)

envoy_cc_library(
    name = "proxy_lib",
    srcs = ["proxy.cc"],
    hdrs = ["proxy.h"],
    deps = [
        ":codec_interface",
        ":codec_lib",
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
        "//source/common/common:logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/network:filter_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/singleton:const_singleton",
        "@envoy_api//envoy/config/filter/network/mongo_proxy/v2:mongo_proxy_cc",
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

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":proxy_lib",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:filter_json_lib",
        "//source/extensions/filters/network:well_known_names",
    ],
)
