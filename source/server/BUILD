load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
    "envoy_proto_library",
    "envoy_select_hot_restart",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "backtrace_lib",
    srcs = ["backtrace.cc"],
    hdrs = ["backtrace.h"],
    tags = ["backtrace"],
    deps = [
        "//source/common/common:minimal_logger_lib",
        "//source/common/version:version_lib",
        "@com_google_absl//absl/debugging:stacktrace",
        "@com_google_absl//absl/debugging:symbolize",
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
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/metrics/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/file/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "listener_stats",
    hdrs = [
        "listener_stats.h",
    ],
    deps = [
        "//source/common/stats:timespan_lib",
    ],
)

envoy_cc_library(
    name = "active_listener_base",
    hdrs = [
        "active_listener_base.h",
    ],
    deps = [
        ":listener_stats",
        "//envoy/network:connection_handler_interface",
        "//envoy/network:listener_interface",
        "//envoy/stats:timespan_interface",
        "//source/common/stats:timespan_lib",
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
        "//source/common/network:listener_lib",
        "//source/common/network:utility_lib",
        "//source/server:active_listener_base",
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
        "@com_google_absl//absl/types:optional",
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
        "//envoy/network:parent_drained_callback_registrar_interface",
        "//source/common/stats:stat_merger_lib",
    ],
)

envoy_cc_library(
    name = "hot_restarting_parent",
    srcs = envoy_select_hot_restart(["hot_restarting_parent.cc"]),
    hdrs = envoy_select_hot_restart(["hot_restarting_parent.h"]),
    deps = [
        ":hot_restarting_base",
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
    name = "options_base",
    srcs = ["options_impl_base.cc"] + select({
        "//bazel:linux_x86_64": ["options_impl_platform_linux.cc"],
        "//bazel:linux_aarch64": ["options_impl_platform_linux.cc"],
        "//bazel:linux_ppc": ["options_impl_platform_linux.cc"],
        "//bazel:linux_mips64": ["options_impl_platform_linux.cc"],
        "//conditions:default": ["options_impl_platform_default.cc"],
    }),
    hdrs = [
        "options_impl_base.h",
        "options_impl_platform.h",
    ] + select({
        "//bazel:linux_x86_64": ["options_impl_platform_linux.h"],
        "//bazel:linux_aarch64": ["options_impl_platform_linux.h"],
        "//bazel:linux_ppc": ["options_impl_platform_linux.h"],
        "//bazel:linux_mips64": ["options_impl_platform_linux.h"],
        "//conditions:default": [],
    }),
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
    name = "options_lib",
    srcs = ["options_impl.cc"],
    hdrs = ["options_impl.h"],
    # TCLAP command line parser needs this to support int64_t/uint64_t in several build environments.
    copts = ["-DHAVE_LONG_LONG"],
    deps = [
        ":options_base",
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
        "@com_github_mirror_tclap//:tclap",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "null_overload_manager_lib",
    hdrs = ["null_overload_manager.h"],
    deps = [
        "//envoy/server/overload:overload_manager_interface",
        "//source/common/event:scaled_range_timer_manager_lib",
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
        "@com_google_absl//absl/container:node_hash_set",
        "@envoy_api//envoy/config/overload/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "api_listener_lib",
    srcs = [
        "api_listener_impl.cc",
    ],
    hdrs = [
        "api_listener_impl.h",
    ],
    deps = [
        ":configuration_lib",
        ":factory_context_lib",
        ":transport_socket_config_lib",
        "//envoy/network:connection_interface",
        "//envoy/network:listener_interface",
        "//envoy/server:filter_config_interface",
        "//envoy/server:instance_interface",
        "//envoy/server:listener_manager_interface",
        "//envoy/server:transport_socket_config_interface",
        "//source/common/common:basic_resource_lib",
        "//source/common/common:empty_string",
        "//source/common/config:metadata_lib",
        "//source/common/config:utility_lib",
        "//source/common/init:manager_lib",
        "//source/common/init:target_lib",
        "//source/common/listener_manager:listener_info_lib",
        "//source/common/network:utility_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/filters/network/http_connection_manager:config",
        "//source/extensions/udp_packet_writer/default:config",
        "//source/extensions/upstreams/http/generic:config",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
    alwayslink = True,
)

envoy_cc_library(
    name = "factory_context_lib",
    srcs = ["factory_context_impl.cc"],
    hdrs = ["factory_context_impl.h"],
    deps = [
        "//envoy/server:factory_context_interface",
        "//envoy/server:instance_interface",
        "//source/common/config:metadata_lib",
        "//source/common/listener_manager:listener_info_lib",
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
    name = "listener_manager_factory_lib",
    hdrs = ["listener_manager_factory.h"],
    deps = [
        "//envoy/server:factory_context_interface",
        "//envoy/server:instance_interface",
        "//envoy/server:listener_manager_interface",
        "//envoy/server:worker_interface",
        "//source/common/quic:quic_stat_names_lib",
    ],
)

envoy_cc_library(
    name = "server_base_lib",
    srcs = ["server.cc"],
    hdrs = ["server.h"],
    rbe_pool = "2core",
    deps = [
        ":api_listener_lib",
        ":configuration_lib",
        ":listener_hooks_lib",
        ":listener_manager_factory_lib",
        ":regex_engine_lib",
        ":utils_lib",
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
        "//envoy/tracing:tracer_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/access_log:access_log_manager_lib",
        "//source/common/api:api_lib",
        "//source/common/common:cleanup_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:mutex_tracer_lib",
        "//source/common/common:perf_tracing_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:utility_lib",
        "//source/common/config:xds_resource_lib",
        "//source/common/grpc:async_client_manager_lib",
        "//source/common/grpc:context_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:context_lib",
        "//source/common/http:headers_lib",
        "//source/common/init:manager_lib",
        "//source/common/local_info:local_info_lib",
        "//source/common/memory:stats_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/quic:quic_stat_names_lib",
        "//source/common/runtime:runtime_keys_lib",
        "//source/common/runtime:runtime_lib",
        "//source/common/secret:secret_manager_impl_lib",
        "//source/common/signal:fatal_error_handler_lib",
        "//source/common/singleton:manager_impl_lib",
        "//source/common/stats:tag_producer_lib",
        "//source/common/stats:thread_local_store_lib",
        "//source/common/tls:context_lib",
        "//source/common/upstream:cluster_manager_lib",
        "//source/common/upstream:health_discovery_service_lib",
        "//source/common/version:version_lib",
        "//source/server/admin:admin_lib",
        "@com_google_absl//absl/container:node_hash_map",
        "@com_google_absl//absl/types:optional",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "server_lib",
    srcs = ["instance_impl.cc"],
    hdrs = ["instance_impl.h"],
    deps = [
        ":guarddog_lib",
        ":server_base_lib",
        "//source/common/memory:heap_shrinker_lib",
        "//source/server:null_overload_manager_lib",
        "//source/server:overload_manager_lib",
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
        ":listener_hooks_lib",
        ":listener_manager_factory_lib",
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
        "//source/common/config:utility_lib",
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
    name = "regex_engine_lib",
    srcs = ["regex_engine.cc"],
    hdrs = ["regex_engine.h"],
    deps = [
        "//source/common/common:regex_lib",
        "//source/common/config:utility_lib",
    ],
)

envoy_cc_library(
    name = "utils_lib",
    srcs = ["utils.cc"],
    hdrs = ["utils.h"],
    deps = [
        "//envoy/common:exception_lib",
        "//envoy/init:manager_interface",
        "//envoy/server:options_interface",
        "//source/common/common:assert_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "generic_factory_context_lib",
    srcs = ["generic_factory_context.cc"],
    hdrs = ["generic_factory_context.h"],
    deps = [
        "//envoy/server:factory_context_interface",
    ],
)
