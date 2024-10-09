load(
    "//bazel:envoy_build_system.bzl",
    "envoy_benchmark_test",
    "envoy_cc_benchmark_binary",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_proto_library(
    name = "config",
    srcs = ["config.proto"],
)

envoy_cc_test(
    name = "listener_impl_test",
    srcs = ["listener_impl_test.cc"],
    data = [
        "internal_listener.yaml",
        "internal_listener_missing_bootstrap.yaml",
    ],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/bootstrap/internal_listener:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//source/extensions/listener_managers/validation_listener_manager:validation_listener_manager_lib",
        "//source/server/config_validation:server_lib",
        "//test/integration:http_integration_lib",
        "//test/mocks/server:options_mocks",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test_library(
    name = "listener_manager_impl_test_lib",
    hdrs = ["listener_manager_impl_test.h"],
    data = ["//test/common/tls/test_data:certs"],
    deps = [
        ":config_cc_proto",
        "//source/common/formatter:formatter_extension_lib",
        "//source/common/init:manager_lib",
        "//source/common/listener_manager:listener_manager_lib",
        "//source/common/tls:server_context_lib",
        "//source/extensions/common/matcher:trie_matcher_lib",
        "//test/mocks/init:init_mocks",
        "//test/mocks/matcher:matcher_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:drain_manager_mocks",
        "//test/mocks/server:guard_dog_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/server:listener_component_factory_mocks",
        "//test/mocks/server:worker_factory_mocks",
        "//test/mocks/server:worker_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:test_time_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "listener_manager_impl_test",
    srcs = ["listener_manager_impl_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":listener_manager_impl_test_lib",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/config:metadata_lib",
        "//source/common/listener_manager:active_raw_udp_listener_config",
        "//source/common/network:addr_family_aware_socket_option_lib",
        "//source/common/network:listen_socket_lib",
        "//source/common/network:socket_option_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf",
        "//source/common/router:string_accessor_lib",
        "//source/common/tls:ssl_socket_lib",
        "//source/extensions/filters/listener/original_dst:config",
        "//source/extensions/filters/listener/proxy_protocol:config",
        "//source/extensions/filters/listener/tls_inspector:config",
        "//source/extensions/filters/network/http_connection_manager:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//source/extensions/matching/network/application_protocol:config",
        "//source/extensions/matching/network/common:inputs_lib",
        "//source/extensions/request_id/uuid:config",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//source/extensions/transport_sockets/tls:config",
        "//test/integration/filters:test_listener_filter_lib",
        "//test/server:utility_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:registry_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)

# Stand-alone quic test because of FIPS.
envoy_cc_test(
    name = "listener_manager_impl_quic_only_test",
    srcs = ["listener_manager_impl_quic_only_test.cc"],
    rbe_pool = "2core",
    tags = ["nofips"],
    deps = [
        ":listener_manager_impl_test_lib",
        "//source/common/formatter:formatter_extension_lib",
        "//source/extensions/filters/http/router:config",
        "//source/extensions/matching/network/common:inputs_lib",
        "//source/extensions/quic/server_preferred_address:fixed_server_preferred_address_config_factory_config",
        "//source/extensions/request_id/uuid:config",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//source/extensions/transport_sockets/tls:config",
        "//test/integration/filters:test_listener_filter_lib",
        "//test/integration/filters:test_network_filter_lib",
        "//test/server:utility_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "filter_chain_manager_impl_test",
    srcs = ["filter_chain_manager_impl_test.cc"],
    data = ["//test/common/tls/test_data:certs"],
    rbe_pool = "2core",
    deps = [
        "//source/common/api:os_sys_calls_lib",
        "//source/common/config:metadata_lib",
        "//source/common/listener_manager:filter_chain_manager_lib",
        "//source/common/listener_manager:listener_manager_lib",
        "//source/common/network:addr_family_aware_socket_option_lib",
        "//source/common/network:listen_socket_lib",
        "//source/common/network:socket_option_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf",
        "//source/common/tls:ssl_socket_lib",
        "//source/extensions/filters/network/http_connection_manager:config",
        "//source/extensions/matching/network/common:inputs_lib",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//source/extensions/transport_sockets/tls:config",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:drain_manager_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/server:utility_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_time_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "lds_api_test",
    srcs = ["lds_api_test.cc"],
    data = [
        "//test/config/integration/certs",
    ],
    rbe_pool = "2core",
    deps = [
        "//source/common/listener_manager:lds_api_lib",
        "//source/common/protobuf:utility_lib",
        "//test/mocks/config:config_mocks",
        "//test/mocks/init:init_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/server:listener_manager_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

envoy_cc_benchmark_binary(
    name = "filter_chain_benchmark_test",
    srcs = ["filter_chain_benchmark_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/listener_manager:filter_chain_manager_lib",
        "//test/test_common:environment_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        # tranport socket config registration
        "//source/extensions/transport_sockets/tls:config",
        "@com_github_google_benchmark//:benchmark",
        "@com_google_googletest//:gtest",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)

envoy_benchmark_test(
    name = "filter_chain_benchmark_test_benchmark_test",
    timeout = "long",
    benchmark_binary = "filter_chain_benchmark_test",
)
