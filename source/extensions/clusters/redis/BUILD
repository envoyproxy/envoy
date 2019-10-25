licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "crc16_lib",
    srcs = [
        "crc16.cc",
        "crc16.h",
    ],
)

envoy_cc_library(
    name = "redis_cluster_lb",
    srcs = [
        "redis_cluster_lb.cc",
        "redis_cluster_lb.h",
    ],
    deps = [
        ":crc16_lib",
        "//include/envoy/upstream:thread_local_cluster_interface",
        "//include/envoy/upstream:upstream_interface",
        "//source/common/common:to_lower_table_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/clusters:well_known_names",
        "//source/extensions/filters/network/common/redis:client_interface",
        "//source/extensions/filters/network/common/redis:codec_interface",
        "//source/extensions/filters/network/common/redis:supported_commands_lib",
    ],
)

envoy_cc_library(
    name = "redis_cluster",
    srcs = [
        "redis_cluster.cc",
        "redis_cluster.h",
    ],
    deps = [
        "redis_cluster_lb",
        "//include/envoy/api:api_interface",
        "//include/envoy/http:codec_interface",
        "//include/envoy/singleton:manager_interface",
        "//include/envoy/upstream:cluster_factory_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//include/envoy/upstream:upstream_interface",
        "//source/common/config:datasource_lib",
        "//source/common/config:metadata_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/network:utility_lib",
        "//source/common/singleton:manager_impl_lib",
        "//source/common/upstream:cluster_factory_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/clusters:well_known_names",
        "//source/extensions/common/redis:redirection_mgr_lib",
        "//source/extensions/filters/network/common/redis:client_interface",
        "//source/extensions/filters/network/common/redis:client_lib",
        "//source/extensions/filters/network/common/redis:codec_interface",
        "//source/extensions/filters/network/common/redis:utility_lib",
        "//source/extensions/filters/network/redis_proxy:config",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//source/server:transport_socket_config_lib",
        "@envoy_api//envoy/config/cluster/redis:pkg_cc_proto",
    ],
)
