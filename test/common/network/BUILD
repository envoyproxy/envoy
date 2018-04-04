licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "address_impl_test",
    srcs = ["address_impl_test.cc"],
    deps = [
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "cidr_range_test",
    srcs = ["cidr_range_test.cc"],
    deps = [
        "//source/common/json:json_loader_lib",
        "//source/common/network:address_lib",
        "//source/common/network:cidr_range_lib",
    ],
)

envoy_cc_test(
    name = "connection_impl_test",
    srcs = ["connection_impl_test.cc"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/common:empty_string",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:connection_lib",
        "//source/common/network:listen_socket_lib",
        "//source/common/network:utility_lib",
        "//source/common/stats:stats_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
    ],
)

envoy_cc_test(
    name = "dns_impl_test",
    srcs = ["dns_impl_test.cc"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/network:dns_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:dns_lib",
        "//source/common/network:filter_lib",
        "//source/common/network:listen_socket_lib",
        "//source/common/stats:stats_lib",
        "//test/mocks/network:network_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "filter_manager_impl_test",
    srcs = ["filter_manager_impl_test.cc"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/config:filter_json_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:filter_manager_lib",
        "//source/common/stats:stats_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/filters/network/ratelimit:ratelimit_lib",
        "//test/common/upstream:utility_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/ratelimit:ratelimit_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/tracing:tracing_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_cc_test(
    name = "lc_trie_test",
    srcs = ["lc_trie_test.cc"],
    deps = [
        "//source/common/common:utility_lib",
        "//source/common/network:address_lib",
        "//source/common/network:cidr_range_lib",
        "//source/common/network:lc_trie_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "listen_socket_impl_test",
    srcs = ["listen_socket_impl_test.cc"],
    deps = [
        "//source/common/network:listen_socket_lib",
        "//source/common/network:utility_lib",
        "//test/mocks/network:network_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "listener_impl_test",
    srcs = ["listener_impl_test.cc"],
    deps = [
        "//source/common/event:dispatcher_lib",
        "//source/common/network:address_lib",
        "//source/common/network:listener_lib",
        "//source/common/network:utility_lib",
        "//source/common/stats:stats_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "resolver_test",
    srcs = ["resolver_impl_test.cc"],
    deps = [
        "//source/common/network:address_lib",
        "//source/common/network:resolver_lib",
        "//source/common/protobuf",
        "//test/mocks/network:network_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:registry_lib",
    ],
)

envoy_cc_test(
    name = "socket_option_impl_test",
    srcs = ["socket_option_impl_test.cc"],
    deps = [
        "//source/common/network:address_lib",
        "//source/common/network:socket_option_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/network:network_mocks",
        "//test/test_common:threadsafe_singleton_injector_lib",
    ],
)

envoy_cc_test(
    name = "utility_test",
    srcs = ["utility_test.cc"],
    deps = [
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//test/test_common:environment_lib",
    ],
)
