licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
    "envoy_proto_library",
    "envoy_select_hot_restart",
)

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
        "//source/common/common:version_lib",
    ],
)

envoy_cc_library(
    name = "configuration_lib",
    srcs = ["configuration_impl.cc"],
    hdrs = ["configuration_impl.h"],
    deps = [
        "//include/envoy/config:typed_config_interface",
        "//include/envoy/http:filter_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/server:configuration_interface",
        "//include/envoy/server:filter_config_interface",
        "//include/envoy/server:instance_interface",
        "//include/envoy/server:tracer_config_interface",
        "//include/envoy/ssl:context_manager_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:runtime_utility_lib",
        "//source/common/config:utility_lib",
        "//source/common/network:resolver_lib",
        "//source/common/network:socket_option_factory_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/metrics/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "connection_handler_lib",
    srcs = ["connection_handler_impl.cc"],
    hdrs = ["connection_handler_impl.h"],
    deps = [
        "//include/envoy/common:time_interface",
        "//include/envoy/event:deferred_deletable",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/event:timer_interface",
        "//include/envoy/network:connection_handler_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/network:listen_socket_interface",
        "//include/envoy/network:listener_interface",
        "//include/envoy/server:active_udp_listener_config_interface",
        "//include/envoy/server:listener_manager_interface",
        "//include/envoy/stats:timespan_interface",
        "//source/common/common:linked_object",
        "//source/common/common:non_copyable",
        "//source/common/network:connection_lib",
        "//source/common/stats:timespan_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/transport_sockets:well_known_names",
    ],
)

envoy_cc_library(
    name = "drain_manager_lib",
    srcs = ["drain_manager_impl.cc"],
    hdrs = [
        "drain_manager_impl.h",
    ],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/event:timer_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/server:drain_manager_interface",
        "//include/envoy/server:instance_interface",
        "//source/common/common:assert_lib",
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
        "//include/envoy/api:api_interface",
        "//include/envoy/event:timer_interface",
        "//include/envoy/server:configuration_interface",
        "//include/envoy/server:guarddog_interface",
        "//include/envoy/server:watchdog_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/thread:thread_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:thread_lib",
        "//source/common/event:libevent_lib",
        "//source/common/stats:symbol_table_lib",
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
        ":hot_restart_cc_proto",
        "//include/envoy/api:os_sys_calls_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/event:file_event_interface",
        "//include/envoy/server:hot_restart_interface",
        "//include/envoy/server:instance_interface",
        "//include/envoy/server:options_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
        "//source/common/network:utility_lib",
    ],
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
        ":api_listener_lib",
        ":hot_restarting_base",
        ":listener_lib",
        "//source/common/memory:stats_lib",
        "//source/common/stats:stat_merger_lib",
        "//source/common/stats:symbol_table_lib",
    ],
)

envoy_cc_library(
    name = "hot_restart_lib",
    srcs = envoy_select_hot_restart(["hot_restart_impl.cc"]),
    hdrs = envoy_select_hot_restart(["hot_restart_impl.h"]),
    deps = [
        ":hot_restarting_child",
        ":hot_restarting_parent",
        "//include/envoy/api:os_sys_calls_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/event:file_event_interface",
        "//include/envoy/server:hot_restart_interface",
        "//include/envoy/server:instance_interface",
        "//include/envoy/server:options_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:assert_lib",
        "//source/common/stats:allocator_lib",
    ],
)

envoy_cc_library(
    name = "hot_restart_nop_lib",
    hdrs = ["hot_restart_nop_impl.h"],
    deps = [
        "//include/envoy/server:hot_restart_interface",
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
        "//include/envoy/network:address_interface",
        "//include/envoy/registry",
        "//include/envoy/server:options_interface",
        "//include/envoy/stats:stats_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:macros",
        "//source/common/common:version_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/stats:stats_lib",
        "@envoy_api//envoy/config/bootstrap/v2:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "overload_manager_lib",
    srcs = ["overload_manager_impl.cc"],
    hdrs = ["overload_manager_impl.h"],
    deps = [
        "//include/envoy/server:overload_manager_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//source/common/common:logger_lib",
        "//source/common/config:utility_lib",
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
        "//include/envoy/config:discovery_service_base_interface",
        "//include/envoy/config:subscription_factory_interface",
        "//include/envoy/config:subscription_interface",
        "//include/envoy/init:manager_interface",
        "//include/envoy/server:listener_manager_interface",
        "//source/common/common:cleanup_lib",
        "//source/common/config:api_version_lib",
        "//source/common/config:resources_lib",
        "//source/common/config:utility_lib",
        "//source/common/init:target_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/api/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
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
        ":drain_manager_lib",
        ":filter_chain_manager_lib",
        ":listener_manager_impl",
        "//include/envoy/network:connection_interface",
        "//include/envoy/server:api_listener_interface",
        "//include/envoy/server:filter_config_interface",
        "//include/envoy/server:listener_manager_interface",
        "//source/common/common:empty_string",
        "//source/common/http:conn_manager_lib",
        "//source/common/init:manager_lib",
        "//source/common/network:resolver_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/filters/network/http_connection_manager:config",
        "@envoy_api//envoy/api/v2:pkg_cc_proto",
        "@envoy_api//envoy/api/v2/listener:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "listener_lib",
    srcs = [
        "listener_impl.cc",
    ],
    hdrs = [
        "listener_impl.h",
    ],
    deps = [
        ":configuration_lib",
        ":drain_manager_lib",
        ":filter_chain_manager_lib",
        ":listener_manager_impl",
        ":transport_socket_config_lib",
        ":well_known_names_lib",
        "//include/envoy/access_log:access_log_interface",
        "//include/envoy/server:active_udp_listener_config_interface",
        "//include/envoy/server:filter_config_interface",
        "//include/envoy/server:listener_manager_interface",
        "//include/envoy/server:transport_socket_config_interface",
        "//source/common/access_log:access_log_lib",
        "//source/common/config:utility_lib",
        "//source/common/init:manager_lib",
        "//source/common/init:target_lib",
        "//source/common/network:connection_balancer_lib",
        "//source/common/network:listen_socket_lib",
        "//source/common/network:socket_option_factory_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/listener:well_known_names",
        "//source/extensions/transport_sockets:well_known_names",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)

# TODO(junr03): actually separate this lib from the listener and api listener lib.
# this can be done if the parent_ in the listener and the api listener becomes the ListenerManager interface.
# the issue right now is that the listener's reach into the listener manager's server_ instance variable.
envoy_cc_library(
    name = "listener_manager_impl",
    srcs = [
        "listener_manager_impl.cc",
    ],
    hdrs = [
        "api_listener_impl.h",
        "listener_impl.h",
        "listener_manager_impl.h",
    ],
    deps = [
        ":configuration_lib",
        ":drain_manager_lib",
        ":filter_chain_manager_lib",
        ":lds_api_lib",
        ":transport_socket_config_lib",
        ":well_known_names_lib",
        "//include/envoy/server:active_udp_listener_config_interface",
        "//include/envoy/server:filter_config_interface",
        "//include/envoy/server:listener_manager_interface",
        "//include/envoy/server:transport_socket_config_interface",
        "//include/envoy/server:worker_interface",
        "//source/common/config:utility_lib",
        "//source/common/config:version_converter_lib",
        "//source/common/http:conn_manager_lib",
        "//source/common/init:manager_lib",
        "//source/common/network:listen_socket_lib",
        "//source/common/network:socket_option_factory_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/listener:well_known_names",
        "//source/extensions/filters/network/http_connection_manager:config",
        "//source/extensions/transport_sockets:well_known_names",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "filter_chain_manager_lib",
    srcs = ["filter_chain_manager_impl.cc"],
    hdrs = ["filter_chain_manager_impl.h"],
    deps = [
        ":filter_chain_factory_context_callback",
        "//include/envoy/server:instance_interface",
        "//include/envoy/server:listener_manager_interface",
        "//include/envoy/server:transport_socket_config_interface",
        "//source/common/common:empty_string",
        "//source/common/config:utility_lib",
        "//source/common/init:manager_lib",
        "//source/common/network:cidr_range_lib",
        "//source/common/network:lc_trie_lib",
        "//source/server:configuration_lib",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "process_context_lib",
    hdrs = ["process_context_impl.h"],
    deps = ["//include/envoy/server:process_context_interface"],
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
        "//include/envoy/server:resource_monitor_config_interface",
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
        ":api_listener_lib",
        ":configuration_lib",
        ":connection_handler_lib",
        ":guarddog_lib",
        ":listener_hooks_lib",
        ":listener_lib",
        ":ssl_context_manager_lib",
        ":worker_lib",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/event:signal_interface",
        "//include/envoy/event:timer_interface",
        "//include/envoy/network:dns_interface",
        "//include/envoy/server:drain_manager_interface",
        "//include/envoy/server:instance_interface",
        "//include/envoy/server:listener_manager_interface",
        "//include/envoy/server:options_interface",
        "//include/envoy/server:process_context_interface",
        "//include/envoy/stats:stats_macros",
        "//include/envoy/tracing:http_tracer_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/access_log:access_log_manager_lib",
        "//source/common/api:api_lib",
        "//source/common/common:cleanup_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:mutex_tracer_lib",
        "//source/common/common:utility_lib",
        "//source/common/common:version_lib",
        "//source/common/config:utility_lib",
        "//source/common/grpc:async_client_manager_lib",
        "//source/common/grpc:context_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:context_lib",
        "//source/common/init:manager_lib",
        "//source/common/local_info:local_info_lib",
        "//source/common/memory:heap_shrinker_lib",
        "//source/common/memory:stats_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/router:rds_lib",
        "//source/common/runtime:runtime_lib",
        "//source/common/secret:secret_manager_impl_lib",
        "//source/common/singleton:manager_impl_lib",
        "//source/common/stats:symbol_table_creator_lib",
        "//source/common/stats:thread_local_store_lib",
        "//source/common/upstream:cluster_manager_lib",
        "//source/common/upstream:health_discovery_service_lib",
        "//source/server:overload_manager_lib",
        "//source/server/http:admin_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "ssl_context_manager_lib",
    srcs = ["ssl_context_manager.cc"],
    hdrs = ["ssl_context_manager.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/ssl:context_manager_interface",
    ],
)

envoy_cc_library(
    name = "listener_hooks_lib",
    hdrs = ["listener_hooks.h"],
)

envoy_cc_library(
    name = "watchdog_lib",
    srcs = ["watchdog_impl.cc"],
    hdrs = ["watchdog_impl.h"],
    deps = [
        "//include/envoy/common:time_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/server:watchdog_interface",
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
        "//include/envoy/api:api_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/event:timer_interface",
        "//include/envoy/server:configuration_interface",
        "//include/envoy/server:guarddog_interface",
        "//include/envoy/server:listener_manager_interface",
        "//include/envoy/server:worker_interface",
        "//include/envoy/thread:thread_interface",
        "//include/envoy/thread_local:thread_local_interface",
    ],
)

envoy_cc_library(
    name = "transport_socket_config_lib",
    hdrs = ["transport_socket_config_impl.h"],
    deps = [
        "//include/envoy/server:transport_socket_config_interface",
    ],
)

envoy_cc_library(
    name = "well_known_names_lib",
    hdrs = ["well_known_names.h"],
    deps = ["//source/common/singleton:const_singleton"],
)

envoy_cc_library(
    name = "active_raw_udp_listener_config",
    srcs = ["active_raw_udp_listener_config.cc"],
    hdrs = ["active_raw_udp_listener_config.h"],
    deps = [
        ":connection_handler_lib",
        ":well_known_names_lib",
        "//include/envoy/registry",
        "//include/envoy/server:active_udp_listener_config_interface",
        "@envoy_api//envoy/api/v2/listener:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "filter_chain_factory_context_callback",
    hdrs = ["filter_chain_factory_context_callback.h"],
    deps = [
        "//include/envoy/server:filter_config_interface",
        "@envoy_api//envoy/api/v2/listener:pkg_cc_proto",
    ],
)
