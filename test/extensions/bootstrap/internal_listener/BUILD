load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "client_connection_factory_test",
    srcs = ["client_connection_factory_test.cc"],
    extension_names = ["envoy.bootstrap.internal_listener"],
    deps = [
        "//source/common/common:utility_lib",
        "//source/common/network:address_lib",
        "//source/extensions/bootstrap/internal_listener:client_connection_factory",
        "//source/extensions/io_socket/user_space:io_handle_impl_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
    ],
)

envoy_extension_cc_test(
    name = "internal_listener_registry_test",
    size = "large",
    srcs = ["internal_listener_registry_test.cc"],
    extension_names = ["envoy.bootstrap.internal_listener"],
    deps = [
        "//source/extensions/bootstrap/internal_listener:config",
        "//test/mocks/thread_local:thread_local_mocks",
    ],
)

envoy_extension_cc_test(
    name = "internal_listener_integration_test",
    size = "large",
    srcs = [
        "internal_listener_integration_test.cc",
    ],
    extension_names = ["envoy.bootstrap.internal_listener"],
    deps = [
        "//source/common/config:api_version_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:connection_lib",
        "//source/common/network:utility_lib",
        "//source/extensions/bootstrap/internal_listener:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//source/extensions/io_socket/user_space:config",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/config:v2_link_hacks",
        "//test/integration:http_integration_lib",
        "//test/integration/filters:address_restore_listener_filter_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:resources_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/tcp_proxy/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "active_internal_listener_test",
    size = "large",
    srcs = ["active_internal_listener_test.cc"],
    extension_names = ["envoy.bootstrap.internal_listener"],
    deps = [
        "//source/common/listener_manager:connection_handler_lib",
        "//source/common/network:address_lib",
        "//source/common/network:connection_balancer_lib",
        "//source/common/network:listen_socket_lib",
        "//source/common/network:utility_lib",
        "//source/common/stats:stats_lib",
        "//source/extensions/bootstrap/internal_listener:active_internal_listener",
        "//source/extensions/bootstrap/internal_listener:thread_local_registry",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/network:network_mocks",
        "//test/server:utility_lib",
        "//test/test_common:test_runtime_lib",
    ],
)
