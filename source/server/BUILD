licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
    "envoy_select_hot_restart",
)

envoy_package()

envoy_cc_library(
    name = "backtrace_lib",
    hdrs = ["backtrace.h"],
    external_deps = [
        "backward",
        "abseil_symbolize",
    ],
    tags = ["backtrace"],
    deps = ["//source/common/common:minimal_logger_lib"],
)

envoy_cc_library(
    name = "configuration_lib",
    srcs = ["configuration_impl.cc"],
    hdrs = ["configuration_impl.h"],
    deps = [
        "//include/envoy/http:filter_interface",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/server:configuration_interface",
        "//include/envoy/server:filter_config_interface",
        "//include/envoy/server:instance_interface",
        "//include/envoy/ssl:context_manager_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/config:lds_json_lib",
        "//source/common/config:utility_lib",
        "//source/common/network:resolver_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/ratelimit:ratelimit_lib",
        "//source/common/tracing:http_tracer_lib",
        "@envoy_api//envoy/api/v2:lds_cc",
        "@envoy_api//envoy/config/bootstrap/v2:bootstrap_cc",
        "@envoy_api//envoy/config/trace/v2:trace_cc",
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
        "//include/envoy/server:listener_manager_interface",
        "//include/envoy/stats:timespan",
        "//source/common/common:linked_object",
        "//source/common/common:non_copyable",
        "//source/common/network:connection_lib",
        "//source/extensions/transport_sockets:well_known_names",
    ],
)

envoy_cc_library(
    name = "drain_manager_lib",
    srcs = ["drain_manager_impl.cc"],
    hdrs = ["drain_manager_impl.h"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/event:timer_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/server:drain_manager_interface",
        "//include/envoy/server:instance_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_library(
    name = "guarddog_lib",
    srcs = ["guarddog_impl.cc"],
    hdrs = ["guarddog_impl.h"],
    external_deps = ["abseil_optional"],
    deps = [
        ":watchdog_lib",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/server:configuration_interface",
        "//include/envoy/server:guarddog_interface",
        "//include/envoy/server:watchdog_interface",
        "//include/envoy/stats:stats_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:thread_lib",
        "//source/common/event:libevent_lib",
    ],
)

envoy_cc_library(
    name = "hot_restart_lib",
    srcs = envoy_select_hot_restart(["hot_restart_impl.cc"]),
    hdrs = envoy_select_hot_restart(["hot_restart_impl.h"]),
    deps = [
        "//include/envoy/api:os_sys_calls_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/event:file_event_interface",
        "//include/envoy/server:hot_restart_interface",
        "//include/envoy/server:instance_interface",
        "//include/envoy/server:options_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:block_memory_hash_set_lib",
        "//source/common/common:utility_lib",
        "//source/common/network:utility_lib",
        "//source/common/stats:raw_stat_data_lib",
        "//source/common/stats:stats_options_lib",
    ],
)

envoy_cc_library(
    name = "hot_restart_nop_lib",
    hdrs = ["hot_restart_nop_impl.h"],
    deps = [
        "//include/envoy/server:hot_restart_interface",
    ],
)

envoy_cc_library(
    name = "init_manager_lib",
    srcs = ["init_manager_impl.cc"],
    hdrs = ["init_manager_impl.h"],
    deps = [
        "//include/envoy/init:init_interface",
        "//source/common/common:assert_lib",
    ],
)

envoy_cc_library(
    name = "options_lib",
    srcs = ["options_impl.cc"],
    hdrs = ["options_impl.h"],
    external_deps = ["tclap"],
    deps = [
        "//include/envoy/network:address_interface",
        "//include/envoy/server:options_interface",
        "//include/envoy/stats:stats_interface",
        "//source/common/common:macros",
        "//source/common/common:version_lib",
        "//source/common/stats:stats_lib",
    ],
)

envoy_cc_library(
    name = "overload_manager_lib",
    srcs = ["overload_manager_impl.cc"],
    hdrs = ["overload_manager_impl.h"],
    deps = [
        "//include/envoy/server:overload_manager_interface",
        "//include/envoy/stats:stats_interface",
        "//source/common/common:logger_lib",
        "//source/common/config:utility_lib",
        "//source/server:resource_monitor_config_lib",
        "@envoy_api//envoy/config/overload/v2alpha:overload_cc",
    ],
)

envoy_cc_library(
    name = "lds_api_lib",
    srcs = ["lds_api.cc"],
    hdrs = ["lds_api.h"],
    deps = [
        ":lds_subscription_lib",
        "//include/envoy/config:subscription_interface",
        "//include/envoy/init:init_interface",
        "//include/envoy/server:listener_manager_interface",
        "//source/common/common:cleanup_lib",
        "//source/common/config:resources_lib",
        "//source/common/config:subscription_factory_lib",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/api/v2:lds_cc",
    ],
)

envoy_cc_library(
    name = "lds_subscription_lib",
    srcs = ["lds_subscription.cc"],
    hdrs = ["lds_subscription.h"],
    deps = [
        "//include/envoy/config:subscription_interface",
        "//source/common/common:assert_lib",
        "//source/common/config:lds_json_lib",
        "//source/common/config:utility_lib",
        "//source/common/http:rest_api_fetcher_lib",
        "//source/common/json:config_schemas_lib",
        "//source/common/json:json_validator_lib",
        "@envoy_api//envoy/api/v2:lds_cc",
    ],
)

envoy_cc_library(
    name = "listener_manager_lib",
    srcs = ["listener_manager_impl.cc"],
    hdrs = ["listener_manager_impl.h"],
    deps = [
        ":configuration_lib",
        ":drain_manager_lib",
        ":init_manager_lib",
        ":lds_api_lib",
        "//include/envoy/server:filter_config_interface",
        "//include/envoy/server:listener_manager_interface",
        "//include/envoy/server:transport_socket_config_interface",
        "//include/envoy/server:worker_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:empty_string",
        "//source/common/config:utility_lib",
        "//source/common/network:cidr_range_lib",
        "//source/common/network:lc_trie_lib",
        "//source/common/network:listen_socket_lib",
        "//source/common/network:resolver_lib",
        "//source/common/network:socket_option_factory_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/ssl:context_config_lib",
        "//source/extensions/filters/listener:well_known_names",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/transport_sockets:well_known_names",
        "@envoy_api//envoy/admin/v2alpha:config_dump_cc",
        "@envoy_api//envoy/api/v2:lds_cc",
    ],
)

envoy_cc_library(
    name = "proto_descriptors_lib",
    srcs = ["proto_descriptors.cc"],
    hdrs = ["proto_descriptors.h"],
    deps = [
        "//source/common/config:protobuf_link_hacks",
        "//source/common/protobuf",
        "@envoy_api//envoy/api/v2:cds_cc",
        "@envoy_api//envoy/api/v2:eds_cc",
        "@envoy_api//envoy/api/v2:lds_cc",
        "@envoy_api//envoy/api/v2:rds_cc",
        "@envoy_api//envoy/service/accesslog/v2:als_cc",
        "@envoy_api//envoy/service/discovery/v2:ads_cc",
        "@envoy_api//envoy/service/discovery/v2:hds_cc",
        "@envoy_api//envoy/service/metrics/v2:metrics_service_cc",
        "@envoy_api//envoy/service/ratelimit/v2:rls_cc",
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
    external_deps = ["abseil_optional"],
    deps = [
        ":configuration_lib",
        ":connection_handler_lib",
        ":guarddog_lib",
        ":init_manager_lib",
        ":listener_manager_lib",
        ":test_hooks_lib",
        ":worker_lib",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/event:signal_interface",
        "//include/envoy/event:timer_interface",
        "//include/envoy/network:dns_interface",
        "//include/envoy/server:drain_manager_interface",
        "//include/envoy/server:instance_interface",
        "//include/envoy/server:listener_manager_interface",
        "//include/envoy/server:options_interface",
        "//include/envoy/stats:stats_macros",
        "//include/envoy/tracing:http_tracer_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/access_log:access_log_manager_lib",
        "//source/common/api:api_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:utility_lib",
        "//source/common/common:version_lib",
        "//source/common/config:bootstrap_json_lib",
        "//source/common/config:utility_lib",
        "//source/common/grpc:async_client_manager_lib",
        "//source/common/local_info:local_info_lib",
        "//source/common/memory:stats_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/router:rds_lib",
        "//source/common/runtime:runtime_lib",
        "//source/common/secret:secret_manager_impl_lib",
        "//source/common/singleton:manager_impl_lib",
        "//source/common/stats:thread_local_store_lib",
        "//source/common/upstream:cluster_manager_lib",
        "//source/common/upstream:health_discovery_service_lib",
        "//source/server/http:admin_lib",
        "@envoy_api//envoy/config/bootstrap/v2:bootstrap_cc",
    ],
)

envoy_cc_library(
    name = "test_hooks_lib",
    hdrs = ["test_hooks.h"],
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
        ":test_hooks_lib",
        "//include/envoy/api:api_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/event:timer_interface",
        "//include/envoy/server:configuration_interface",
        "//include/envoy/server:guarddog_interface",
        "//include/envoy/server:listener_manager_interface",
        "//include/envoy/server:worker_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//source/common/common:thread_lib",
    ],
)
