licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_select_hot_restart",
)
load(
    "//source/extensions:all_extensions.bzl",
    "envoy_all_extensions",
)

envoy_package()

envoy_cc_test(
    name = "backtrace_test",
    srcs = ["backtrace_test.cc"],
    tags = ["backtrace"],
    deps = ["//source/server:backtrace_lib"],
)

envoy_cc_test(
    name = "configuration_impl_test",
    srcs = ["configuration_impl_test.cc"],
    data = [
        "//test/config/integration/certs",
    ],
    deps = [
        "//source/common/event:dispatcher_lib",
        "//source/common/upstream:cluster_manager_lib",
        "//source/extensions/stat_sinks/statsd:config",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//source/server:configuration_lib",
        "//test/mocks:common_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "connection_handler_test",
    srcs = ["connection_handler_test.cc"],
    deps = [
        "//source/common/common:utility_lib",
        "//source/common/network:address_lib",
        "//source/common/stats:stats_lib",
        "//source/server:connection_handler_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:network_utility_lib",
    ],
)

envoy_cc_test(
    name = "drain_manager_impl_test",
    srcs = ["drain_manager_impl_test.cc"],
    deps = [
        "//source/server:drain_manager_lib",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_cc_test(
    name = "hot_restart_impl_test",
    srcs = envoy_select_hot_restart(["hot_restart_impl_test.cc"]),
    deps = [
        "//source/common/api:os_sys_calls_lib",
        "//source/common/stats:stats_lib",
        "//source/server:hot_restart_lib",
        "//test/mocks/server:server_mocks",
        "//test/test_common:threadsafe_singleton_injector_lib",
    ],
)

envoy_cc_test(
    name = "init_manager_impl_test",
    srcs = ["init_manager_impl_test.cc"],
    deps = [
        "//source/server:init_manager_lib",
        "//test/mocks:common_lib",
        "//test/mocks/init:init_mocks",
    ],
)

envoy_cc_test(
    name = "guarddog_impl_test",
    srcs = ["guarddog_impl_test.cc"],
    deps = [
        "//include/envoy/common:time_interface",
        "//source/common/common:utility_lib",
        "//source/common/stats:stats_lib",
        "//source/server:guarddog_lib",
        "//test/mocks:common_lib",
        "//test/mocks/server:server_mocks",
        "//test/mocks/stats:stats_mocks",
    ],
)

envoy_cc_test(
    name = "options_impl_test",
    srcs = ["options_impl_test.cc"],
    deps = [
        "//source/common/common:utility_lib",
        "//source/common/stats:stats_lib",
        "//source/server:options_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "lds_api_test",
    srcs = ["lds_api_test.cc"],
    data = [
        "//test/config/integration/certs",
    ],
    deps = [
        "//source/common/config:utility_lib",
        "//source/server:lds_api_lib",
        "//test/mocks/server:server_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/api/v2:lds_cc",
    ],
)

envoy_cc_test(
    name = "listener_manager_impl_test",
    srcs = ["listener_manager_impl_test.cc"],
    data = ["//test/common/ssl/test_data:certs"],
    deps = [
        ":utility_lib",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/config:metadata_lib",
        "//source/common/network:addr_family_aware_socket_option_lib",
        "//source/common/network:listen_socket_lib",
        "//source/common/network:socket_option_lib",
        "//source/common/network:utility_lib",
        "//source/common/ssl:ssl_socket_lib",
        "//source/extensions/filters/listener/original_dst:config",
        "//source/extensions/filters/listener/tls_inspector:config",
        "//source/extensions/filters/network/http_connection_manager:config",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//source/extensions/transport_sockets/ssl:config",
        "//source/server:listener_manager_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
    ],
)

envoy_cc_fuzz_test(
    name = "server_fuzz_test",
    srcs = ["server_fuzz_test.cc"],
    corpus = "server_corpus",
    deps = [
        "//source/common/thread_local:thread_local_lib",
        "//source/server:server_lib",
        "//test/integration:integration_lib",
        "//test/mocks/server:server_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:environment_lib",
    ] + envoy_all_extensions(),
)

envoy_cc_test(
    name = "server_test",
    srcs = ["server_test.cc"],
    data = [
        ":cluster_dupe_bootstrap.yaml",
        ":cluster_health_check_bootstrap.yaml",
        ":empty_bootstrap.yaml",
        ":node_bootstrap.yaml",
        "//test/config/integration:server.json",
        "//test/config/integration:server_config_files",
    ],
    deps = [
        "//source/common/common:version_lib",
        "//source/extensions/access_loggers/file:config",
        "//source/extensions/filters/http/buffer:config",
        "//source/extensions/filters/http/dynamo:config",
        "//source/extensions/filters/http/grpc_http1_bridge:config",
        "//source/extensions/filters/http/health_check:config",
        "//source/extensions/filters/http/ratelimit:config",
        "//source/extensions/filters/http/router:config",
        "//source/extensions/filters/network/http_connection_manager:config",
        "//source/extensions/filters/network/redis_proxy:config",
        "//source/extensions/stat_sinks/statsd:config",
        "//source/server:server_lib",
        "//test/integration:integration_lib",
        "//test/mocks/server:server_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test_library(
    name = "utility_lib",
    hdrs = ["utility.h"],
    deps = [
        "//source/common/config:lds_json_lib",
        "//source/common/json:json_loader_lib",
    ],
)

envoy_cc_test(
    name = "worker_impl_test",
    srcs = ["worker_impl_test.cc"],
    deps = [
        "//source/common/event:dispatcher_lib",
        "//source/server:worker_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/test_common:utility_lib",
    ],
)
