load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_mock(
    name = "config_tracker_mocks",
    srcs = ["config_tracker.cc"],
    hdrs = ["config_tracker.h"],
    deps = [
        "//envoy/server:configuration_interface",
    ],
)

envoy_cc_mock(
    name = "admin_mocks",
    srcs = ["admin.cc"],
    hdrs = ["admin.h"],
    deps = [
        ":config_tracker_mocks",
        "//envoy/server:admin_interface",
        "//test/mocks/network:socket_mocks",
    ],
)

envoy_cc_mock(
    name = "bootstrap_extension_factory_mocks",
    srcs = ["bootstrap_extension_factory.cc"],
    hdrs = ["bootstrap_extension_factory.h"],
    deps = [
        "//envoy/server:bootstrap_extension_config_interface",
    ],
)

envoy_cc_mock(
    name = "fatal_action_factory_mocks",
    hdrs = ["fatal_action_factory.h"],
    deps = [
        "//envoy/server:fatal_action_interface",
    ],
)

envoy_cc_mock(
    name = "options_mocks",
    srcs = ["options.cc"],
    hdrs = ["options.h"],
    deps = [
        "//envoy/server:options_interface",
        "//source/common/config:well_known_names",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

envoy_cc_mock(
    name = "admin_stream_mocks",
    srcs = ["admin_stream.cc"],
    hdrs = ["admin_stream.h"],
    deps = [
        "//envoy/server:admin_interface",
        "//test/mocks/http:http_mocks",
    ],
)

envoy_cc_mock(
    name = "drain_manager_mocks",
    srcs = ["drain_manager.cc"],
    hdrs = ["drain_manager.h"],
    deps = [
        "//envoy/server:drain_manager_interface",
    ],
)

envoy_cc_mock(
    name = "watch_dog_mocks",
    srcs = ["watch_dog.cc"],
    hdrs = ["watch_dog.h"],
    deps = [
        "//envoy/server:watchdog_interface",
    ],
)

envoy_cc_mock(
    name = "watchdog_config_mocks",
    srcs = ["watchdog_config.cc"],
    hdrs = ["watchdog_config.h"],
    deps = [
        "//envoy/server:configuration_interface",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_mock(
    name = "guard_dog_mocks",
    srcs = ["guard_dog.cc"],
    hdrs = ["guard_dog.h"],
    deps = [
        ":watch_dog_mocks",
        "//envoy/server:guarddog_interface",
    ],
)

envoy_cc_mock(
    name = "hot_restart_mocks",
    srcs = ["hot_restart.cc"],
    hdrs = ["hot_restart.h"],
    deps = [
        "//envoy/server:instance_interface",
        "//test/mocks/stats:stats_mocks",
    ],
)

envoy_cc_mock(
    name = "listener_component_factory_mocks",
    srcs = ["listener_component_factory.cc"],
    hdrs = ["listener_component_factory.h"],
    deps = [
        "//envoy/server:drain_manager_interface",
        "//envoy/server:listener_manager_interface",
        "//test/mocks/network:io_handle_mocks",
        "//test/mocks/network:network_mocks",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)

envoy_cc_mock(
    name = "listener_manager_mocks",
    srcs = ["listener_manager.cc"],
    hdrs = ["listener_manager.h"],
    deps = [
        "//envoy/server:listener_manager_interface",
    ],
)

envoy_cc_mock(
    name = "server_lifecycle_notifier_mocks",
    srcs = ["server_lifecycle_notifier.cc"],
    hdrs = ["server_lifecycle_notifier.h"],
    deps = [
        "//envoy/server:lifecycle_notifier_interface",
    ],
)

envoy_cc_mock(
    name = "worker_factory_mocks",
    srcs = ["worker_factory.cc"],
    hdrs = ["worker_factory.h"],
    deps = [
        ":worker_mocks",
        "//envoy/server:worker_interface",
    ],
)

envoy_cc_mock(
    name = "worker_mocks",
    srcs = ["worker.cc"],
    hdrs = ["worker.h"],
    deps = [
        "//envoy/server:worker_interface",
    ],
)

envoy_cc_mock(
    name = "overload_manager_mocks",
    srcs = ["overload_manager.cc"],
    hdrs = ["overload_manager.h"],
    deps = [
        "//envoy/server/overload:overload_manager_interface",
        "//test/mocks/event:event_mocks",
    ],
)

envoy_cc_mock(
    name = "instance_mocks",
    srcs = ["instance.cc"],
    hdrs = ["instance.h"],
    deps = [
        ":server_factory_context_mocks",
        ":transport_socket_factory_context_mocks",
        "//envoy/server:instance_interface",
    ],
)

envoy_cc_mock(
    name = "main_mocks",
    hdrs = ["main.h"],
    deps = [
        "//envoy/server:configuration_interface",
        "//envoy/server/overload:overload_manager_interface",
    ],
)

envoy_cc_mock(
    name = "server_factory_context_mocks",
    srcs = ["server_factory_context.cc"],
    hdrs = ["server_factory_context.h"],
    deps = [
        ":admin_mocks",
        ":drain_manager_mocks",
        ":hot_restart_mocks",
        ":listener_manager_mocks",
        ":options_mocks",
        ":overload_manager_mocks",
        ":server_lifecycle_notifier_mocks",
        "//envoy/server:factory_context_interface",
        "//source/common/grpc:context_lib",
        "//source/common/http:context_lib",
        "//source/common/quic:quic_stat_names_lib",
        "//source/common/router:context_lib",
        "//source/common/secret:secret_manager_impl_lib",
        "//source/common/singleton:manager_impl_lib",
        "//source/common/stats:stats_lib",
        "//source/extensions/transport_sockets/tls:context_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/api:api_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/init:init_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/router:router_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/secret:secret_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/tracing:tracing_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
    ],
)

envoy_cc_mock(
    name = "factory_context_mocks",
    srcs = ["factory_context.cc"],
    hdrs = ["factory_context.h"],
    deps = [
        ":drain_manager_mocks",
        ":instance_mocks",
        ":overload_manager_mocks",
        ":server_lifecycle_notifier_mocks",
        "//source/common/router:context_lib",
    ],
)

envoy_cc_mock(
    name = "transport_socket_factory_context_mocks",
    srcs = ["transport_socket_factory_context.cc"],
    hdrs = ["transport_socket_factory_context.h"],
    deps = [
        ":server_factory_context_mocks",
        "//envoy/server:tracer_config_interface",
        "//source/common/secret:secret_manager_impl_lib",
        "//test/mocks/access_log:access_log_mocks",
    ],
)

envoy_cc_mock(
    name = "listener_factory_context_mocks",
    srcs = ["listener_factory_context.cc"],
    hdrs = ["listener_factory_context.h"],
    deps = [
        ":factory_context_mocks",
        "//envoy/server:listener_manager_interface",
    ],
)

envoy_cc_mock(
    name = "health_checker_factory_context_mocks",
    srcs = ["health_checker_factory_context.cc"],
    hdrs = ["health_checker_factory_context.h"],
    deps = [
        ":factory_context_mocks",
        "//envoy/server:health_checker_config_interface",
        "//test/mocks:common_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/api:api_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/router:router_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:cluster_priority_set_mocks",
        "//test/mocks/upstream:health_check_event_logger_mocks",
        "//test/mocks/upstream:health_checker_mocks",
    ],
)

envoy_cc_mock(
    name = "filter_chain_factory_context_mocks",
    srcs = ["filter_chain_factory_context.cc"],
    hdrs = ["filter_chain_factory_context.h"],
    deps = [
        ":factory_context_mocks",
        "//envoy/server:filter_config_interface",
        "//source/common/router:context_lib",
    ],
)

envoy_cc_mock(
    name = "tracer_factory_mocks",
    srcs = ["tracer_factory.cc"],
    hdrs = ["tracer_factory.h"],
    deps = [
        "//envoy/protobuf:message_validator_interface",
        "//envoy/server:tracer_config_interface",
    ],
)

envoy_cc_mock(
    name = "tracer_factory_context_mocks",
    srcs = ["tracer_factory_context.cc"],
    hdrs = ["tracer_factory_context.h"],
    deps = [
        ":instance_mocks",
        ":tracer_factory_mocks",
        "//envoy/server:configuration_interface",
    ],
)

envoy_cc_mock(
    name = "server_mocks",
    srcs = [],
    hdrs = ["mocks.h"],
    deps = [
        ":admin_mocks",
        ":admin_stream_mocks",
        ":bootstrap_extension_factory_mocks",
        ":config_tracker_mocks",
        ":drain_manager_mocks",
        ":factory_context_mocks",
        ":fatal_action_factory_mocks",
        ":filter_chain_factory_context_mocks",
        ":guard_dog_mocks",
        ":health_checker_factory_context_mocks",
        ":hot_restart_mocks",
        ":instance_mocks",
        ":listener_component_factory_mocks",
        ":listener_factory_context_mocks",
        ":listener_manager_mocks",
        ":main_mocks",
        ":options_mocks",
        ":overload_manager_mocks",
        ":server_lifecycle_notifier_mocks",
        ":tracer_factory_context_mocks",
        ":tracer_factory_mocks",
        ":transport_socket_factory_context_mocks",
        ":watch_dog_mocks",
        ":worker_factory_mocks",
        ":worker_mocks",
    ],
)
