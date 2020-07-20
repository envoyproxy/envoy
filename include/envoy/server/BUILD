load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "access_log_config_interface",
    hdrs = ["access_log_config.h"],
    deps = [
        ":filter_config_interface",
        "//include/envoy/access_log:access_log_interface",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "admin_interface",
    hdrs = ["admin.h"],
    deps = [
        ":config_tracker_interface",
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/http:filter_interface",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/http:query_params_interface",
        "//include/envoy/network:listen_socket_interface",
    ],
)

envoy_cc_library(
    name = "api_listener_interface",
    hdrs = ["api_listener.h"],
    deps = ["//include/envoy/http:api_listener_interface"],
)

envoy_cc_library(
    name = "configuration_interface",
    hdrs = ["configuration.h"],
    external_deps = ["abseil_optional"],
    deps = [
        "//include/envoy/http:context_interface",
        "//include/envoy/ratelimit:ratelimit_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "config_tracker_interface",
    hdrs = ["config_tracker.h"],
    deps = [
        "//source/common/common:non_copyable",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "drain_manager_interface",
    hdrs = ["drain_manager.h"],
    deps = ["//include/envoy/network:drain_decision_interface"],
)

envoy_cc_library(
    name = "guarddog_interface",
    hdrs = ["guarddog.h"],
    deps = [
        "//include/envoy/server:watchdog_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/thread:thread_interface",
    ],
)

envoy_cc_library(
    name = "health_checker_config_interface",
    hdrs = ["health_checker_config.h"],
    deps = [
        "//include/envoy/common:random_generator_interface",
        "//include/envoy/config:typed_config_interface",
        "//include/envoy/upstream:health_checker_interface",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "hot_restart_interface",
    hdrs = ["hot_restart.h"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/thread:thread_interface",
        "//source/server:hot_restart_cc_proto",
    ],
)

envoy_cc_library(
    name = "instance_interface",
    hdrs = ["instance.h"],
    deps = [
        ":admin_interface",
        ":drain_manager_interface",
        ":hot_restart_interface",
        ":lifecycle_notifier_interface",
        ":listener_manager_interface",
        ":options_interface",
        "//include/envoy/access_log:access_log_interface",
        "//include/envoy/api:api_interface",
        "//include/envoy/common:mutex_tracer",
        "//include/envoy/event:timer_interface",
        "//include/envoy/http:context_interface",
        "//include/envoy/http:query_params_interface",
        "//include/envoy/init:manager_interface",
        "//include/envoy/local_info:local_info_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/secret:secret_manager_interface",
        "//include/envoy/server:overload_manager_interface",
        "//include/envoy/ssl:context_manager_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//include/envoy/tracing:http_tracer_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "options_interface",
    hdrs = ["options.h"],
    deps = [
        "//include/envoy/network:address_interface",
        "//include/envoy/stats:stats_interface",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "worker_interface",
    hdrs = ["worker.h"],
    deps = [
        ":overload_manager_interface",
        "//include/envoy/server:guarddog_interface",
    ],
)

envoy_cc_library(
    name = "watchdog_interface",
    hdrs = ["watchdog.h"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/network:address_interface",
        "//include/envoy/thread:thread_interface",
    ],
)

envoy_cc_library(
    name = "factory_context_interface",
    hdrs = ["factory_context.h"],
    deps = [
        ":admin_interface",
        ":drain_manager_interface",
        ":lifecycle_notifier_interface",
        ":process_context_interface",
        "//include/envoy/access_log:access_log_interface",
        "//include/envoy/api:api_interface",
        "//include/envoy/config:typed_config_interface",
        "//include/envoy/grpc:context_interface",
        "//include/envoy/http:codes_interface",
        "//include/envoy/http:context_interface",
        "//include/envoy/http:filter_interface",
        "//include/envoy/init:manager_interface",
        "//include/envoy/local_info:local_info_interface",
        "//include/envoy/network:drain_decision_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/server:overload_manager_interface",
        "//include/envoy/singleton:manager_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//include/envoy/tracing:http_tracer_interface",
        "//include/envoy/upstream:cluster_manager_interface",
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
        "//include/envoy/access_log:access_log_interface",
        "//include/envoy/config:typed_config_interface",
        "//include/envoy/http:codes_interface",
        "//include/envoy/http:filter_interface",
        "//include/envoy/server:overload_manager_interface",
        "//include/envoy/server:transport_socket_config_interface",
        "//include/envoy/singleton:manager_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//include/envoy/tracing:http_tracer_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:macros",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "lifecycle_notifier_interface",
    hdrs = ["lifecycle_notifier.h"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
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
        "//include/envoy/network:filter_interface",
        "//include/envoy/network:listen_socket_interface",
        "//include/envoy/ssl:context_interface",
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
        "//include/envoy/config:typed_config_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/init:manager_interface",
        "//include/envoy/local_info:local_info_interface",
        "//include/envoy/network:transport_socket_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/secret:secret_manager_interface",
        "//include/envoy/singleton:manager_interface",
        "//include/envoy/ssl:context_manager_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/protobuf",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "resource_monitor_interface",
    hdrs = ["resource_monitor.h"],
    deps = [
        "//include/envoy/config:typed_config_interface",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "request_id_extension_config_interface",
    hdrs = ["request_id_extension_config.h"],
    deps = [
        ":filter_config_interface",
        "//include/envoy/http:request_id_extension_interface",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "resource_monitor_config_interface",
    hdrs = ["resource_monitor_config.h"],
    deps = [
        ":resource_monitor_interface",
        "//include/envoy/api:api_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/protobuf:message_validator_interface",
    ],
)

envoy_cc_library(
    name = "overload_manager_interface",
    hdrs = ["overload_manager.h"],
    deps = [
        "//include/envoy/thread_local:thread_local_interface",
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_library(
    name = "tracer_config_interface",
    hdrs = ["tracer_config.h"],
    deps = [
        ":filter_config_interface",
        "//include/envoy/config:typed_config_interface",
        "//source/common/protobuf",
    ],
)

envoy_cc_library(
    name = "active_udp_listener_config_interface",
    hdrs = ["active_udp_listener_config.h"],
    deps = [
        "//include/envoy/config:typed_config_interface",
        "//include/envoy/network:connection_handler_interface",
    ],
)

envoy_cc_library(
    name = "bootstrap_extension_config_interface",
    hdrs = ["bootstrap_extension_config.h"],
    deps = [
        ":factory_context_interface",
        "//include/envoy/config:typed_config_interface",
    ],
)
