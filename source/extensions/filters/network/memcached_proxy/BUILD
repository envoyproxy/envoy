licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "codec_interface",
    hdrs = ["codec.h"],
    deps = [],
)

envoy_cc_library(
    name = "codec_lib",
    srcs = ["codec_impl.cc"],
    hdrs = ["codec_impl.h"],
    deps = [
        ":codec_interface",
        "//include/envoy/buffer:buffer_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/network:filter_lib",
        "//source/common/config:filter_json_lib",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":proxy_lib",
        "//include/envoy/registry",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
    ],
)

envoy_cc_library(
    name = "proxy_lib",
    srcs = ["proxy.cc"],
    hdrs = ["proxy.h"],
    deps = [
        ":codec_interface",
        ":codec_lib",
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
        "//source/extensions/filters/network:well_known_names",
        "@envoy_api//envoy/config/filter/network/memcached_proxy/v2:memcached_proxy_cc",
    ],
)
