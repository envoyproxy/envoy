load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "admin_interface",
    hdrs = ["admin.h"],
    deps = [
        ":config_tracker_interface",
        "//envoy/buffer:buffer_interface",
        "//envoy/http:filter_interface",
        "//envoy/http:header_map_interface",
        "//envoy/http:query_params_interface",
        "//envoy/network:listen_socket_interface",
    ],
)

envoy_cc_library(
    name = "api_listener_interface",
    hdrs = ["api_listener.h"],
    deps = ["//envoy/http:api_listener_interface"],
)

envoy_cc_library(
    name = "configuration_interface",
    hdrs = ["configuration.h"],
    deps = [
        "//envoy/http:context_interface",
        "//envoy/stats:sink_interface",
        "//envoy/upstream:cluster_manager_interface",
        "@com_google_absl//absl/types:optional",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "config_tracker_interface",
    hdrs = ["config_tracker.h"],
    deps = [
        "//envoy/common:matchers_interface",
        "//source/common/common:non_copyable",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "drain_manager_interface",
    hdrs = ["drain_manager.h"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/network:drain_decision_interface",
        "//envoy/thread_local:thread_local_object",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "guarddog_interface",
    hdrs = ["guarddog.h"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/server:watchdog_interface",
        "//envoy/stats:stats_interface",
        "//envoy/thread:thread_interface",
    ],
)

envoy_cc_library(
    name = "guarddog_config_interface",
    hdrs = ["guarddog_config.h"],
    deps = [
        ":guarddog_interface",
        "//envoy/api:api_interface",
        "//envoy/protobuf:message_validator_interface",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "health_checker_config_interface",
    hdrs = ["health_checker_config.h"],
    deps = [
        ":factory_context_interface",
        "//envoy/common:random_generator_interface",
        "//envoy/config:typed_config_interface",
        "//envoy/upstream:health_checker_interface",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "hot_restart_interface",
    hdrs = ["hot_restart.h"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/thread:thread_interface",
    ],
)

envoy_cc_library(
    name = "instance_interface",
    hdrs = ["instance.h"],
    deps = [
        ":admin_interface",
        ":configuration_interface",
        ":drain_manager_interface",
        ":hot_restart_interface",
        ":lifecycle_notifier_interface",
        ":listener_manager_interface",
        ":options_interface",
        "//envoy/access_log:access_log_interface",
        "//envoy/api:api_interface",
        "//envoy/common:mutex_tracer",
        "//envoy/event:timer_interface",
        "//envoy/http:context_interface",
        "//envoy/http:query_params_interface",
        "//envoy/init:manager_interface",
        "//envoy/local_info:local_info_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/secret:secret_manager_interface",
        "//envoy/server/overload:overload_manager_interface",
        "//envoy/ssl:context_manager_interface",
        "//envoy/thread_local:thread_local_interface",
        "//envoy/tracing:tracer_interface",
        "//envoy/upstream:cluster_manager_interface",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "options_interface",
    hdrs = ["options.h"],
    deps = [
        "//envoy/network:address_interface",
        "//envoy/stats:stats_interface",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "worker_interface",
    hdrs = ["worker.h"],
    deps = [
        "//envoy/runtime:runtime_interface",
        "//envoy/server:guarddog_interface",
        "//envoy/server/overload:overload_manager_interface",
    ],
)

envoy_cc_library(
    name = "watchdog_interface",
    hdrs = ["watchdog.h"],
    deps = [
        "//envoy/common:base_includes",
        "//envoy/thread:thread_interface",
    ],
)

envoy_cc_library(
    name = "factory_context_interface",
    hdrs = ["factory_context.h"],
    deps = [
        ":admin_interface",
        ":configuration_interface",
        ":drain_manager_interface",
        ":lifecycle_notifier_interface",
        ":options_interface",
        ":process_context_interface",
        "//envoy/access_log:access_log_interface",
        "//envoy/api:api_interface",
        "//envoy/config:typed_config_interface",
        "//envoy/config:typed_metadata_interface",
        "//envoy/grpc:context_interface",
        "//envoy/http:codes_interface",
        "//envoy/http:context_interface",
        "//envoy/http:filter_interface",
        "//envoy/http:http_server_properties_cache_interface",
        "//envoy/init:manager_interface",
        "//envoy/local_info:local_info_interface",
        "//envoy/network:drain_decision_interface",
        "//envoy/router:context_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/server/overload:overload_manager_interface",
        "//envoy/singleton:manager_interface",
        "//envoy/thread_local:thread_local_interface",
        "//envoy/tracing:tracer_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:macros",
        "//source/common/protobuf",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "filter_config_interface",
    hdrs = ["filter_config.h"],
    deps = [
        ":drain_manager_interface",
        ":factory_context_interface",
        ":lifecycle_notifier_interface",
        ":process_context_interface",
        "//envoy/access_log:access_log_interface",
        "//envoy/config:typed_config_interface",
        "//envoy/http:codes_interface",
        "//envoy/http:filter_interface",
        "//envoy/server:transport_socket_config_interface",
        "//envoy/server/overload:overload_manager_interface",
        "//envoy/singleton:manager_interface",
        "//envoy/thread_local:thread_local_interface",
        "//envoy/tracing:tracer_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:macros",
        "//source/common/protobuf",
        "@envoy_api//envoy/extensions/filters/common/dependency/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "lifecycle_notifier_interface",
    hdrs = ["lifecycle_notifier.h"],
    deps = [
        "//envoy/event:dispatcher_interface",
    ],
)

envoy_cc_library(
    name = "listener_manager_interface",
    hdrs = ["listener_manager.h"],
    deps = [
        ":api_listener_interface",
        ":drain_manager_interface",
        ":filter_config_interface",
        ":guarddog_interface",
        "//envoy/filter:config_provider_manager_interface",
        "//envoy/network:filter_interface",
        "//envoy/network:listen_socket_interface",
        "//envoy/network:socket_interface_interface",
        "//envoy/ssl:context_interface",
        "//source/common/protobuf",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "process_context_interface",
    hdrs = ["process_context.h"],
)

envoy_cc_library(
    name = "transport_socket_config_interface",
    hdrs = ["transport_socket_config.h"],
    deps = [
        ":factory_context_interface",
        "//envoy/config:typed_config_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/init:manager_interface",
        "//envoy/local_info:local_info_interface",
        "//envoy/network:transport_socket_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/secret:secret_manager_interface",
        "//envoy/singleton:manager_interface",
        "//envoy/ssl:context_manager_interface",
        "//envoy/stats:stats_interface",
        "//envoy/thread_local:thread_local_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/protobuf",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "resource_monitor_interface",
    hdrs = ["resource_monitor.h"],
    deps = [
        "//envoy/config:typed_config_interface",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "proactive_resource_monitor_interface",
    hdrs = ["proactive_resource_monitor.h"],
    deps = [
        "//source/common/stats:symbol_table_lib",
    ],
)

envoy_cc_library(
    name = "request_id_extension_config_interface",
    hdrs = ["request_id_extension_config.h"],
    deps = [
        ":filter_config_interface",
        "//envoy/http:request_id_extension_interface",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "resource_monitor_config_interface",
    hdrs = ["resource_monitor_config.h"],
    deps = [
        ":options_interface",
        ":proactive_resource_monitor_interface",
        ":resource_monitor_interface",
        "//envoy/api:api_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/protobuf:message_validator_interface",
    ],
)

envoy_cc_library(
    name = "tracer_config_interface",
    hdrs = ["tracer_config.h"],
    deps = [
        ":filter_config_interface",
        "//envoy/config:typed_config_interface",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "bootstrap_extension_config_interface",
    hdrs = ["bootstrap_extension_config.h"],
    deps = [
        ":factory_context_interface",
        "//envoy/config:typed_config_interface",
    ],
)

envoy_cc_library(
    name = "fatal_action_interface",
    hdrs = ["fatal_action_config.h"],
    deps = [
        "//envoy/config:typed_config_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/protobuf:message_validator_interface",
        "//envoy/server:instance_interface",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "platform_interface",
    hdrs = ["platform.h"],
    deps = [
        "//envoy/filesystem:filesystem_interface",
        "//envoy/thread:thread_interface",
    ],
)
