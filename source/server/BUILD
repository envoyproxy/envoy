load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
    "envoy_proto_library",
    "envoy_select_enable_http3",
    "envoy_select_hot_restart",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "backtrace_lib",
    srcs = ["backtrace.cc"],
    hdrs = ["backtrace.h"],
    external_deps = [
        "abseil_stacktrace",
        "abseil_symbolize",
    ],
    tags = ["backtrace"],
    deps = [
        "//source/common/common:minimal_logger_lib",
        "//source/common/version:version_lib",
    ],
)

envoy_cc_library(
    name = "configuration_lib",
    srcs = ["configuration_impl.cc"],
    hdrs = ["configuration_impl.h"],
    deps = [
        "//envoy/config:typed_config_interface",
        "//envoy/filter:config_provider_manager_interface",
        "//envoy/http:filter_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/server:configuration_interface",
        "//envoy/server:filter_config_interface",
        "//envoy/server:instance_interface",
        "//envoy/server:tracer_config_interface",
        "//envoy/ssl:context_manager_interface",
        "//source/common/access_log:access_log_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:runtime_utility_lib",
        "//source/common/config:utility_lib",
        "//source/common/network:default_socket_interface_lib",
        "//source/common/network:resolver_lib",
        "//source/common/network:socket_interface_lib",
        "//source/common/network:socket_option_factory_lib",
        "//source/common/network:socket_option_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/access_loggers/common:file_access_log_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/metrics/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "connection_handler_lib",
    deps = [
        ":active_tcp_listener",
        ":active_udp_listener",
        ":connection_handler_impl",
        "//envoy/network:client_connection_factory",
        "//source/common/network:address_lib",
    ],
)

envoy_cc_library(
    name = "connection_handler_impl",
    srcs = ["connection_handler_impl.cc"],
    hdrs = [
        "connection_handler_impl.h",
    ],
    deps = [
        ":active_tcp_listener",
        "//envoy/common:time_interface",
        "//envoy/event:deferred_deletable",
        "//envoy/event:dispatcher_interface",
        "//envoy/network:connection_handler_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "//envoy/network:listen_socket_interface",
        "//envoy/network:listener_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/server:listener_manager_interface",
        "//envoy/stats:timespan_interface",
        "//source/common/common:linked_object",
        "//source/common/common:non_copyable",
        "//source/common/event:deferred_task",
        "//source/common/network:connection_lib",
        "//source/common/stream_info:stream_info_lib",
    ],
)

envoy_cc_library(
    name = "active_listener_base",
    hdrs = [
        "active_listener_base.h",
    ],
    deps = [
        "//envoy/network:connection_handler_interface",
        "//envoy/network:listener_interface",
        "//envoy/stats:timespan_interface",
        "//source/common/stats:timespan_lib",
    ],
)

envoy_cc_library(
    name = "active_tcp_listener",
    srcs = ["active_tcp_listener.cc"],
    hdrs = [
        "active_tcp_listener.h",
    ],
    deps = [
        ":active_stream_listener_base",
        ":active_tcp_socket",
        "//envoy/common:time_interface",
        "//envoy/event:deferred_deletable",
        "//envoy/event:dispatcher_interface",
        "//envoy/network:connection_handler_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:listen_socket_interface",
        "//envoy/network:listener_interface",
        "//envoy/server:listener_manager_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:linked_object",
        "//source/common/network:connection_lib",
        "//source/common/stats:timespan_lib",
        "//source/server:active_listener_base",
    ],
)

envoy_cc_library(
    name = "active_udp_listener",
    srcs = ["active_udp_listener.cc"],
    hdrs = [
        "active_udp_listener.h",
    ],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/network:connection_handler_interface",
        "//envoy/network:exception_interface",
        "//envoy/network:filter_interface",
        "//envoy/network:listen_socket_interface",
        "//envoy/network:listener_interface",
        "//envoy/server:listener_manager_interface",
        "//source/server:connection_handler_impl",
    ],
)

envoy_cc_library(
    name = "active_tcp_listener_headers",
    hdrs = [
        "active_stream_listener_base.h",
        "active_tcp_listener.h",
        "active_tcp_socket.h",
    ],
    deps = [
        ":active_listener_base",
        "//envoy/common:time_interface",
        "//envoy/event:deferred_deletable",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/network:connection_handler_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "//envoy/network:listen_socket_interface",
        "//envoy/network:listener_interface",
        "//envoy/server:listener_manager_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:linked_object",
        "//source/common/network:connection_lib",
        "//source/common/stats:timespan_lib",
    ],
)

envoy_cc_library(
    name = "active_tcp_socket",
    srcs = ["active_tcp_socket.cc"],
    hdrs = [
        "active_tcp_socket.h",
    ],
    deps = [
        ":active_listener_base",
        ":active_tcp_listener_headers",
        "//envoy/common:time_interface",
        "//envoy/event:deferred_deletable",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/network:connection_handler_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "//envoy/network:listen_socket_interface",
        "//envoy/network:listener_interface",
        "//source/common/common:linked_object",
        "//source/common/network:connection_lib",
        "//source/common/network:listener_filter_buffer_lib",
        "//source/common/stream_info:stream_info_lib",
    ],
)

envoy_cc_library(
    name = "active_stream_listener_base",
    srcs = ["active_stream_listener_base.cc"],
    hdrs = [
        "active_stream_listener_base.h",
    ],
    deps = [
        ":active_listener_base",
        ":active_tcp_listener_headers",
        "//envoy/common:time_interface",
        "//envoy/event:deferred_deletable",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/network:connection_handler_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "//envoy/network:listen_socket_interface",
        "//envoy/network:listener_interface",
        "//envoy/stream_info:stream_info_interface",
        "//source/common/common:linked_object",
        "//source/common/network:connection_lib",
        "//source/common/network:listener_filter_buffer_lib",
    ],
)

envoy_cc_library(
    name = "drain_manager_lib",
    srcs = ["drain_manager_impl.cc"],
    hdrs = [
        "drain_manager_impl.h",
    ],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/server:drain_manager_interface",
        "//envoy/server:instance_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:callback_impl_lib",
        "//source/common/common:minimal_logger_lib",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "guarddog_lib",
    srcs = ["guarddog_impl.cc"],
    hdrs = ["guarddog_impl.h"],
    external_deps = ["abseil_optional"],
    deps = [
        ":watchdog_lib",
        "//envoy/api:api_interface",
        "//envoy/common:time_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/server:configuration_interface",
        "//envoy/server:guarddog_config_interface",
        "//envoy/server:guarddog_interface",
        "//envoy/server:watchdog_interface",
        "//envoy/stats:stats_interface",
        "//envoy/thread:thread_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:thread_lib",
        "//source/common/config:utility_lib",
        "//source/common/event:libevent_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/stats:symbol_table_lib",
        "//source/common/watchdog:abort_action_config",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/watchdog/v3:pkg_cc_proto",
    ],
)

envoy_proto_library(
    name = "hot_restart",
    srcs = ["hot_restart.proto"],
)

envoy_cc_library(
    name = "hot_restarting_base",
    srcs = envoy_select_hot_restart(["hot_restarting_base.cc"]),
    hdrs = envoy_select_hot_restart(["hot_restarting_base.h"]),
    deps = [
        "//envoy/api:os_sys_calls_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:file_event_interface",
        "//envoy/server:hot_restart_interface",
        "//envoy/server:instance_interface",
        "//envoy/server:options_interface",
        "//envoy/stats:stats_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:mem_block_builder_lib",
        "//source/common/common:safe_memcpy_lib",
        "//source/common/common:utility_lib",
        "//source/common/network:utility_lib",
        "//source/common/stats:utility_lib",
    ] + envoy_select_hot_restart([":hot_restart_cc_proto"]),
)

envoy_cc_library(
    name = "hot_restarting_child",
    srcs = envoy_select_hot_restart(["hot_restarting_child.cc"]),
    hdrs = envoy_select_hot_restart(["hot_restarting_child.h"]),
    deps = [
        ":hot_restarting_base",
        "//source/common/stats:stat_merger_lib",
    ],
)

envoy_cc_library(
    name = "hot_restarting_parent",
    srcs = envoy_select_hot_restart(["hot_restarting_parent.cc"]),
    hdrs = envoy_select_hot_restart(["hot_restarting_parent.h"]),
    deps = [
        ":hot_restarting_base",
        ":listener_manager_lib",
        "//source/common/memory:stats_lib",
        "//source/common/stats:stat_merger_lib",
        "//source/common/stats:symbol_table_lib",
        "//source/common/stats:utility_lib",
    ],
)

envoy_cc_library(
    name = "hot_restart_lib",
    srcs = envoy_select_hot_restart(["hot_restart_impl.cc"]),
    hdrs = envoy_select_hot_restart(["hot_restart_impl.h"]),
    deps = [
        ":hot_restarting_child",
        ":hot_restarting_parent",
        "//envoy/api:os_sys_calls_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:file_event_interface",
        "//envoy/server:hot_restart_interface",
        "//envoy/server:instance_interface",
        "//envoy/server:options_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:assert_lib",
        "//source/common/stats:allocator_lib",
    ],
)

envoy_cc_library(
    name = "hot_restart_nop_lib",
    hdrs = ["hot_restart_nop_impl.h"],
    deps = [
        "//envoy/server:hot_restart_interface",
        "//source/common/stats:allocator_lib",
    ],
)

envoy_cc_library(
    name = "options_lib",
    srcs = ["options_impl.cc"] + select({
        "//bazel:linux_x86_64": ["options_impl_platform_linux.cc"],
        "//bazel:linux_aarch64": ["options_impl_platform_linux.cc"],
        "//bazel:linux_ppc": ["options_impl_platform_linux.cc"],
        "//bazel:linux_mips64": ["options_impl_platform_linux.cc"],
        "//conditions:default": ["options_impl_platform_default.cc"],
    }),
    hdrs = [
        "options_impl.h",
        "options_impl_platform.h",
    ] + select({
        "//bazel:linux_x86_64": ["options_impl_platform_linux.h"],
        "//bazel:linux_aarch64": ["options_impl_platform_linux.h"],
        "//bazel:linux_ppc": ["options_impl_platform_linux.h"],
        "//bazel:linux_mips64": ["options_impl_platform_linux.h"],
        "//conditions:default": [],
    }),
    # TCLAP command line parser needs this to support int64_t/uint64_t in several build environments.
    copts = ["-DHAVE_LONG_LONG"],
    external_deps = ["tclap"],
    deps = [
        "//envoy/network:address_interface",
        "//envoy/registry",
        "//envoy/server:options_interface",
        "//envoy/stats:stats_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:macros",
        "//source/common/protobuf:utility_lib",
        "//source/common/stats:stats_lib",
        "//source/common/stats:tag_utility_lib",
        "//source/common/version:version_lib",
    ],
)

envoy_cc_library(
    name = "overload_manager_lib",
    srcs = ["overload_manager_impl.cc"],
    hdrs = ["overload_manager_impl.h"],
    deps = [
        "//envoy/server/overload:overload_manager_interface",
        "//envoy/stats:stats_interface",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/common:logger_lib",
        "//source/common/config:utility_lib",
        "//source/common/event:scaled_range_timer_manager_lib",
        "//source/common/stats:symbol_table_lib",
        "//source/server:resource_monitor_config_lib",
        "@envoy_api//envoy/config/overload/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "lds_api_lib",
    srcs = ["lds_api.cc"],
    hdrs = ["lds_api.h"],
    deps = [
        "//envoy/config:subscription_factory_interface",
        "//envoy/config:subscription_interface",
        "//envoy/init:manager_interface",
        "//envoy/server:listener_manager_interface",
        "//source/common/common:cleanup_lib",
        "//source/common/config:api_version_lib",
        "//source/common/config:subscription_base_interface",
        "//source/common/config:utility_lib",
        "//source/common/init:target_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

# TODO(junr03): actually separate this lib from the listener and api listener lib.
# this can be done if the parent_ in the listener and the api listener becomes the ListenerManager interface.
# the issue right now is that the listener's reach into the listener manager's server_ instance variable.
envoy_cc_library(
    name = "listener_manager_lib",
    srcs = [
        "api_listener_impl.cc",
        "listener_impl.cc",
        "listener_manager_impl.cc",
    ],
    hdrs = [
        "api_listener_impl.h",
        "listener_impl.h",
        "listener_manager_impl.h",
    ],
    deps = [
        "active_raw_udp_listener_config",
        ":configuration_lib",
        ":drain_manager_lib",
        ":filter_chain_manager_lib",
        ":lds_api_lib",
        ":transport_socket_config_lib",
        "//envoy/access_log:access_log_interface",
        "//envoy/config:typed_metadata_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:listener_interface",
        "//envoy/server:api_listener_interface",
        "//envoy/server:filter_config_interface",
        "//envoy/server:listener_manager_interface",
        "//envoy/server:transport_socket_config_interface",
        "//envoy/server:worker_interface",
        "//source/common/access_log:access_log_lib",
        "//source/common/common:basic_resource_lib",
        "//source/common/common:empty_string",
        "//source/common/config:utility_lib",
        "//source/common/config:metadata_lib",
        "//source/common/http:conn_manager_lib",
        "//source/common/init:manager_lib",
        "//source/common/init:target_lib",
        "//source/common/network:connection_balancer_lib",
        "//source/common/network:filter_matcher_lib",
        "//source/common/network:listen_socket_lib",
        "//source/common/network:listener_lib",
        "//source/common/network:resolver_lib",
        "//source/common/network:socket_option_factory_lib",
        "//source/common/network:udp_packet_writer_handler_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/common/quic:quic_stat_names_lib",
        "//source/extensions/filters/network/http_connection_manager:config",
        "//source/extensions/upstreams/http/generic:config",
        "//source/extensions/udp_packet_writer/default:config",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/listener/proxy_protocol/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/raw_buffer/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/udp_packet_writer/v3:pkg_cc_proto",
    ] + envoy_select_enable_http3([
        "//source/common/quic:active_quic_listener_lib",
        "//source/common/quic:client_connection_factory_lib",
        "//source/common/quic:quic_factory_lib",
        "//source/common/quic:quic_transport_socket_factory_lib",
        "//source/common/quic:udp_gso_batch_writer_lib",
        "//source/extensions/udp_packet_writer/gso:config",
    ]),
)

envoy_cc_library(
    name = "filter_chain_manager_lib",
    srcs = ["filter_chain_manager_impl.cc"],
    hdrs = ["filter_chain_manager_impl.h"],
    deps = [
        ":filter_chain_factory_context_callback",
        "//envoy/config:typed_metadata_interface",
        "//envoy/matcher:matcher_interface",
        "//envoy/network:filter_interface",
        "//envoy/server:instance_interface",
        "//envoy/server:listener_manager_interface",
        "//envoy/server:transport_socket_config_interface",
        "//source/common/common:empty_string",
        "//source/common/config:utility_lib",
        "//source/common/init:manager_lib",
        "//source/common/matcher:matcher_lib",
        "//source/common/network:cidr_range_lib",
        "//source/common/network:lc_trie_lib",
        "//source/common/network/matching:data_impl_lib",
        "//source/common/network/matching:inputs_lib",
        "//source/server:configuration_lib",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "process_context_lib",
    hdrs = ["process_context_impl.h"],
    deps = ["//envoy/server:process_context_interface"],
)

envoy_cc_library(
    name = "proto_descriptors_lib",
    srcs = ["proto_descriptors.cc"],
    hdrs = ["proto_descriptors.h"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/config:protobuf_link_hacks",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "resource_monitor_config_lib",
    hdrs = ["resource_monitor_config_impl.h"],
    deps = [
        "//envoy/server:resource_monitor_config_interface",
    ],
)

envoy_cc_library(
    name = "factory_context_base_impl_lib",
    hdrs = ["factory_context_base_impl.h"],
    deps = [
        "//envoy/server:factory_context_interface",
    ],
)

envoy_cc_library(
    name = "server_lib",
    srcs = ["server.cc"],
    hdrs = ["server.h"],
    external_deps = [
        "abseil_node_hash_map",
        "abseil_optional",
    ],
    deps = [
        ":active_raw_udp_listener_config",
        ":configuration_lib",
        ":connection_handler_lib",
        ":guarddog_lib",
        ":listener_hooks_lib",
        ":listener_manager_lib",
        ":ssl_context_manager_lib",
        ":worker_lib",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:signal_interface",
        "//envoy/event:timer_interface",
        "//envoy/network:dns_interface",
        "//envoy/server:bootstrap_extension_config_interface",
        "//envoy/server:configuration_interface",
        "//envoy/server:drain_manager_interface",
        "//envoy/server:fatal_action_interface",
        "//envoy/server:instance_interface",
        "//envoy/server:listener_manager_interface",
        "//envoy/server:options_interface",
        "//envoy/server:process_context_interface",
        "//envoy/stats:stats_macros",
        "//envoy/tracing:http_tracer_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/access_log:access_log_manager_lib",
        "//source/common/api:api_lib",
        "//source/common/common:cleanup_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:mutex_tracer_lib",
        "//source/common/common:perf_tracing_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:grpc_mux_lib",
        "//source/common/config:new_grpc_mux_lib",
        "//source/common/config:utility_lib",
        "//source/common/config:xds_resource_lib",
        "//source/common/config/xds_mux:grpc_mux_lib",
        "//source/common/grpc:async_client_manager_lib",
        "//source/common/grpc:context_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:context_lib",
        "//source/common/http:headers_lib",
        "//source/common/init:manager_lib",
        "//source/common/local_info:local_info_lib",
        "//source/common/memory:heap_shrinker_lib",
        "//source/common/memory:stats_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/quic:quic_stat_names_lib",
        "//source/common/router:rds_lib",
        "//source/common/runtime:runtime_lib",
        "//source/common/secret:secret_manager_impl_lib",
        "//source/common/signal:fatal_error_handler_lib",
        "//source/common/singleton:manager_impl_lib",
        "//source/common/stats:thread_local_store_lib",
        "//source/common/upstream:cluster_manager_lib",
        "//source/common/upstream:health_discovery_service_lib",
        "//source/common/version:version_lib",
        "//source/server:overload_manager_lib",
        "//source/server/admin:admin_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "ssl_context_manager_lib",
    srcs = ["ssl_context_manager.cc"],
    hdrs = ["ssl_context_manager.h"],
    deps = [
        "//envoy/registry",
        "//envoy/ssl:context_manager_interface",
    ],
)

envoy_cc_library(
    name = "listener_hooks_lib",
    hdrs = ["listener_hooks.h"],
)

envoy_cc_library(
    name = "watchdog_lib",
    hdrs = ["watchdog_impl.h"],
    deps = [
        "//envoy/common:time_interface",
        "//envoy/server:watchdog_interface",
        "//source/common/common:assert_lib",
    ],
)

envoy_cc_library(
    name = "worker_lib",
    srcs = ["worker_impl.cc"],
    hdrs = ["worker_impl.h"],
    deps = [
        ":connection_handler_lib",
        ":listener_hooks_lib",
        "//envoy/api:api_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:timer_interface",
        "//envoy/network:exception_interface",
        "//envoy/server:configuration_interface",
        "//envoy/server:guarddog_interface",
        "//envoy/server:listener_manager_interface",
        "//envoy/server:worker_interface",
        "//envoy/thread:thread_interface",
        "//envoy/thread_local:thread_local_interface",
    ],
)

envoy_cc_library(
    name = "transport_socket_config_lib",
    hdrs = ["transport_socket_config_impl.h"],
    deps = [
        "//envoy/server:transport_socket_config_interface",
    ],
)

envoy_cc_library(
    name = "active_raw_udp_listener_config",
    srcs = ["active_raw_udp_listener_config.cc"],
    hdrs = ["active_raw_udp_listener_config.h"],
    deps = [
        ":connection_handler_lib",
        "//envoy/registry",
    ],
)

envoy_cc_library(
    name = "filter_chain_factory_context_callback",
    hdrs = ["filter_chain_factory_context_callback.h"],
    deps = [
        "//envoy/server:filter_config_interface",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)
