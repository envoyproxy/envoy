load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Redis proxy L4 network filter. Implements consistent hashing and observability for large redis
# clusters.
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/listeners/network_filters/redis_proxy_filter

envoy_extension_package()

envoy_cc_library(
    name = "command_splitter_interface",
    hdrs = ["command_splitter.h"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/stream_info:stream_info_interface",
        "//source/extensions/filters/network/common/redis:client_interface",
        "//source/extensions/filters/network/common/redis:codec_interface",
    ],
)

envoy_cc_library(
    name = "config_interface",
    hdrs = ["config.h"],
    deps = [
        "//source/common/config:datasource_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/redis_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "conn_pool_interface",
    hdrs = ["conn_pool.h"],
    deps = [
        "//envoy/upstream:cluster_manager_interface",
        "//source/extensions/filters/network/common/redis:client_interface",
        "//source/extensions/filters/network/common/redis:codec_interface",
    ],
)

envoy_cc_library(
    name = "router_interface",
    hdrs = ["router.h"],
    deps = [
        ":conn_pool_interface",
        "//envoy/stream_info:stream_info_interface",
    ],
)

envoy_cc_library(
    name = "command_splitter_lib",
    srcs = ["command_splitter_impl.cc"],
    hdrs = ["command_splitter_impl.h"],
    deps = [
        ":command_splitter_interface",
        ":conn_pool_lib",
        ":router_interface",
        "//envoy/stats:stats_macros",
        "//envoy/stats:timespan_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:trie_lookup_table_lib",
        "//source/common/stats:timespan_lib",
        "//source/extensions/filters/network/common/redis:client_lib",
        "//source/extensions/filters/network/common/redis:fault_lib",
        "//source/extensions/filters/network/common/redis:supported_commands_lib",
        "//source/extensions/filters/network/common/redis:utility_lib",
    ],
)

envoy_cc_library(
    name = "conn_pool_lib",
    srcs = ["conn_pool_impl.cc"],
    hdrs = ["conn_pool_impl.h"],
    deps = [
        ":config_interface",
        ":conn_pool_interface",
        "//envoy/stats:stats_macros",
        "//envoy/thread_local:thread_local_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:token_bucket_impl_lib",
        "//source/common/network:address_lib",
        "//source/common/network:filter_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/upstream:load_balancer_context_base_lib",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/clusters/redis:redis_cluster_lb",
        "//source/extensions/common/dynamic_forward_proxy:dns_cache_interface",
        "//source/extensions/common/redis:cluster_refresh_manager_interface",
        "//source/extensions/filters/network/common/redis:client_lib",
        "//source/extensions/filters/network/common/redis:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/redis_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "external_auth_lib",
    srcs = ["external_auth.cc"],
    hdrs = ["external_auth.h"],
    deps = [
        ":command_splitter_lib",
        "//source/common/grpc:async_client_lib",
        "//source/common/grpc:typed_async_client_lib",
        "@envoy_api//envoy/service/redis_auth/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "proxy_filter_lib",
    srcs = ["proxy_filter.cc"],
    hdrs = ["proxy_filter.h"],
    deps = [
        ":command_splitter_interface",
        ":external_auth_lib",
        "//envoy/network:drain_decision_interface",
        "//envoy/network:filter_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/config:datasource_lib",
        "//source/common/config:utility_lib",
        "//source/extensions/common/dynamic_forward_proxy:dns_cache_interface",
        "//source/extensions/filters/network/common/redis:codec_interface",
        "@envoy_api//envoy/extensions/filters/network/redis_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":command_splitter_lib",
        ":conn_pool_lib",
        ":proxy_filter_lib",
        ":router_lib",
        "//envoy/upstream:upstream_interface",
        "//source/extensions/common/dynamic_forward_proxy:dns_cache_manager_impl",
        "//source/extensions/common/redis:cluster_refresh_manager_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "//source/extensions/filters/network/common/redis:codec_lib",
        "//source/extensions/filters/network/common/redis:fault_lib",
        "//source/extensions/filters/network/common/redis:redis_command_stats_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/redis_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "router_lib",
    srcs = ["router_impl.cc"],
    hdrs = ["router_impl.h"],
    deps = [
        ":conn_pool_lib",
        ":router_interface",
        "//envoy/stream_info:stream_info_interface",
        "//envoy/thread_local:thread_local_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:trie_lookup_table_lib",
        "//source/extensions/filters/network/common/redis:codec_lib",
        "//source/extensions/filters/network/common/redis:supported_commands_lib",
        "//source/extensions/filters/network/common/redis:utility_lib",
        "@envoy_api//envoy/extensions/filters/network/redis_proxy/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)
