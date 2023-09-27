load(
    "//bazel:envoy_build_system.bzl",
    "envoy_benchmark_test",
    "envoy_cc_benchmark_binary",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "listener_impl_test_base_lib",
    hdrs = ["listener_impl_test_base.h"],
    deps = [
        "//source/common/event:dispatcher_lib",
        "//source/common/network:address_lib",
        "//source/common/network:listener_lib",
        "//source/common/network:utility_lib",
        "//source/common/stats:stats_lib",
        "//source/common/stream_info:stream_info_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "address_impl_test",
    srcs = ["address_impl_test.cc"],
    deps = [
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//test/mocks/api:api_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_benchmark_binary(
    name = "address_impl_speed_test",
    srcs = ["address_impl_speed_test.cc"],
    external_deps = [
        "benchmark",
    ],
    deps = [
        "//source/common/network:address_lib",
    ],
)

envoy_benchmark_test(
    name = "address_impl_speed_test_benchmark_test",
    benchmark_binary = "address_impl_speed_test",
)

envoy_cc_test(
    name = "cidr_range_test",
    srcs = ["cidr_range_test.cc"],
    deps = [
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
        "//test/mocks/api:api_mocks",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:test_time_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "happy_eyeballs_connection_provider_test",
    srcs = ["happy_eyeballs_connection_provider_test.cc"],
    deps = [
        "//source/common/network:happy_eyeballs_connection_impl_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "multi_connection_base_impl_test",
    srcs = ["multi_connection_base_impl_test.cc"],
    deps = [
        "//source/common/network:multi_connection_base_impl_lib",
        "//source/common/network:socket_option_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/network:network_mocks",
    ],
)

envoy_cc_test(
    name = "filter_manager_impl_test",
    srcs = ["filter_manager_impl_test.cc"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:filter_manager_lib",
        "//source/common/stats:stats_lib",
        "//source/common/tcp_proxy",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//test/common/upstream:utility_lib",
        "//test/extensions/filters/common/ratelimit:ratelimit_mocks",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/ratelimit:ratelimit_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/tracing:tracing_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/network/tcp_proxy/v3:pkg_cc_proto",
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
        "//source/common/network:address_lib",
        "//source/common/network:listen_socket_lib",
        "//source/common/network:utility_lib",
        "//test/mocks/network:io_handle_mocks",
        "//test/mocks/network:network_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "listener_filter_buffer_impl_test",
    srcs = ["listener_filter_buffer_impl_test.cc"],
    deps = [
        "//source/common/network:listener_filter_buffer_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/network:io_handle_mocks",
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
        "//source/common/stream_info:stream_info_lib",
        "//test/common/network:listener_impl_test_base_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:overload_manager_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "raw_buffer_socket_test",
    srcs = ["raw_buffer_socket_test.cc"],
    deps = [
        "//source/common/network:raw_buffer_socket_lib",
        "//source/common/network:transport_socket_options_lib",
        "//test/test_common:network_utility_lib",
    ],
)

envoy_cc_test_library(
    name = "udp_listener_impl_test_base_lib",
    hdrs = ["udp_listener_impl_test_base.h"],
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
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "udp_listener_impl_test",
    srcs = ["udp_listener_impl_test.cc"],
    deps = [
        ":udp_listener_impl_test_base_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:address_lib",
        "//source/common/network:listener_lib",
        "//source/common/network:socket_option_lib",
        "//source/common/network:udp_packet_writer_handler_lib",
        "//source/common/network:utility_lib",
        "//source/common/stats:stats_lib",
        "//test/common/network:listener_impl_test_base_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "udp_listener_impl_batch_writer_test",
    srcs = ["udp_listener_impl_batch_writer_test.cc"],
    # Skipping as quiche quic_gso_batch_writer.h does not exist on Windows
    tags = [
        "nofips",
        "skip_on_windows",
    ],
    deps = [
        ":udp_listener_impl_test_base_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:address_lib",
        "//source/common/network:listener_lib",
        "//source/common/network:socket_option_lib",
        "//source/common/network:udp_packet_writer_handler_lib",
        "//source/common/network:utility_lib",
        "//source/common/quic:udp_gso_batch_writer_lib",
        "//source/common/stats:stats_lib",
        "//test/common/network:listener_impl_test_base_lib",
        "//test/mocks/network:network_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "//test/test_common:utility_lib",
        "@com_github_google_quiche//:quic_test_tools_mock_syscall_wrapper_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
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
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "socket_option_test",
    hdrs = ["socket_option_test.h"],
    deps = [
        "//source/common/network:address_lib",
        "//source/common/network:socket_option_factory_lib",
        "//source/common/network:socket_option_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/network:network_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "socket_option_impl_test",
    srcs = ["socket_option_impl_test.cc"],
    deps = [
        ":socket_option_test",
        "//test/test_common:environment_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "socket_option_factory_test",
    srcs = ["socket_option_factory_test.cc"],
    external_deps = ["abseil_str_format"],
    deps = [
        "//source/common/network:address_lib",
        "//source/common/network:socket_option_factory_lib",
        "//source/common/network:socket_option_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/network:network_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "addr_family_aware_socket_option_impl_test",
    srcs = ["addr_family_aware_socket_option_impl_test.cc"],
    deps = [
        ":socket_option_test",
        "//source/common/network:addr_family_aware_socket_option_lib",
        "//source/common/network:address_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "utility_test",
    srcs = ["utility_test.cc"],
    deps = [
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/network:network_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_fuzz_test(
    name = "udp_fuzz",
    srcs = ["udp_fuzz.cc"],
    corpus = "udp_corpus",
    deps = [
        "udp_listener_impl_test_base_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:address_lib",
        "//source/common/network:listener_lib",
        "//source/common/network:socket_option_lib",
        "//source/common/network:udp_packet_writer_handler_lib",
        "//source/common/network:utility_lib",
        "//source/common/stats:stats_lib",
        "//test/common/network:listener_impl_test_base_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_fuzz_test(
    name = "utility_fuzz_test",
    srcs = ["utility_fuzz_test.cc"],
    corpus = "utility_corpus",
    deps = [
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_benchmark_binary(
    name = "lc_trie_speed_test",
    srcs = ["lc_trie_speed_test.cc"],
    external_deps = [
        "benchmark",
    ],
    deps = [
        "//source/common/network:lc_trie_lib",
        "//source/common/network:utility_lib",
    ],
)

envoy_benchmark_test(
    name = "lc_trie_speed_test_benchmark_test",
    benchmark_binary = "lc_trie_speed_test",
)

envoy_cc_test(
    name = "io_socket_handle_impl_test",
    srcs = ["io_socket_handle_impl_test.cc"],
    deps = [
        "//source/common/common:utility_lib",
        "//source/common/network:address_lib",
        "//test/mocks/api:api_mocks",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "win32_socket_handle_impl_test",
    srcs = ["win32_socket_handle_impl_test.cc"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/common:utility_lib",
        "//source/common/network:address_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/event:event_mocks",
        "//test/test_common:threadsafe_singleton_injector_lib",
    ],
)

envoy_cc_test(
    name = "io_socket_handle_impl_integration_test",
    srcs = ["io_socket_handle_impl_integration_test.cc"],
    tags = ["requires-network"],
    deps = [
        "//source/common/common:utility_lib",
        "//source/common/network:address_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "transport_socket_options_impl_test",
    srcs = ["transport_socket_options_impl_test.cc"],
    deps = [
        "//source/common/network:address_lib",
        "//source/common/network:transport_socket_options_lib",
        "//source/common/stream_info:filter_state_lib",
    ],
)

envoy_cc_test(
    name = "win32_redirect_records_option_test",
    srcs = ["win32_redirect_records_option_test.cc"],
    deps = [
        ":socket_option_test",
        "//source/common/network:address_lib",
        "//source/common/network:win32_redirect_records_option_lib",
    ],
)

envoy_cc_test(
    name = "filter_matcher_test",
    srcs = ["filter_matcher_test.cc"],
    deps = [
        "//source/common/network:address_lib",
        "//source/common/network:filter_matcher_lib",
        "//test/mocks/network:network_mocks",
    ],
)

envoy_proto_library(
    name = "listener_filter_buffer_fuzz_proto",
    srcs = ["listener_filter_buffer_fuzz.proto"],
)

envoy_cc_fuzz_test(
    name = "listener_filter_buffer_fuzz_test",
    srcs = ["listener_filter_buffer_fuzz_test.cc"],
    corpus = "listener_filter_buffer_corpus",
    deps = [
        ":listener_filter_buffer_fuzz_proto_cc_proto",
        "//source/common/network:listener_filter_buffer_lib",
        "//test/fuzz:utility_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/network:io_handle_mocks",
    ],
)
