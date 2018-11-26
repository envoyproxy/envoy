licenses(["notice"])  # Apache 2

load("//bazel:envoy_build_system.bzl", "envoy_cc_fuzz_test", "envoy_cc_test", "envoy_package")
load(
    "//source/extensions:all_extensions.bzl",
    "envoy_all_extensions",
)

envoy_package()

envoy_cc_test(
    name = "async_client_test",
    srcs = ["async_client_test.cc"],
    deps = [
        "//include/envoy/http:message_interface",
        "//source/common/http:message_lib",
        "//source/server/config_validation:async_client_lib",
        "//source/server/config_validation:dns_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:test_time_lib",
    ],
)

envoy_cc_test(
    name = "cluster_manager_test",
    srcs = ["cluster_manager_test.cc"],
    deps = [
        "//include/envoy/upstream:resource_manager_interface",
        "//include/envoy/upstream:upstream_interface",
        "//source/common/api:api_lib",
        "//source/common/ssl:context_lib",
        "//source/common/stats:stats_lib",
        "//source/server/config_validation:cluster_manager_lib",
        "//source/server/config_validation:dns_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/secret:secret_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "server_test",
    srcs = ["server_test.cc"],
    data = [
        "//configs:example_configs",
        "//test/config_test:example_configs_test_setup.sh",
    ],
    deps = [
        "//source/extensions/filters/http/router:config",
        "//source/extensions/filters/network/http_connection_manager:config",
        "//source/extensions/transport_sockets/ssl:config",
        "//source/server/config_validation:server_lib",
        "//test/integration:integration_lib",
        "//test/mocks/server:server_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "dispatcher_test",
    srcs = ["dispatcher_test.cc"],
    deps = [
        "//source/common/event:libevent_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/server/config_validation:api_lib",
        "//source/server/config_validation:dns_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:test_time_lib",
    ],
)

envoy_cc_fuzz_test(
    name = "config_fuzz_test",
    srcs = ["config_fuzz_test.cc"],
    corpus = "//test/server:server_fuzz_test_corpus",
    deps = [
        "//source/common/common:thread_lib",
        "//source/server/config_validation:server_lib",
        "//source/server:proto_descriptors_lib",
        "//test/integration:integration_lib",
        "//test/mocks/server:server_mocks",
        "//test/test_common:environment_lib",
    ] + envoy_all_extensions(),
)
