load(
    "//bazel:envoy_build_system.bzl",
    "envoy_benchmark_test",
    "envoy_cc_benchmark_binary",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
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
        "//test/mocks/network:io_handle_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_time_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "apple_dns_impl_test",
    srcs = select({
        "//bazel:apple": ["apple_dns_impl_test.cc"],
        "//conditions:default": [],
    }),
    external_deps = ["abseil_synchronization"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/network:dns_interface",
        "//source/common/event:dispatcher_includes",
        "//include/envoy/event:file_event_interface",
        "//source/common/stats:isolated_store_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:address_lib",
        "//source/common/common:random_generator_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "//test/mocks/event:event_mocks",
    ] + select({
        "//bazel:apple": [
            "//source/common/network:dns_lib",
        ],
        "//conditions:default": [],
    }),
)

envoy_cc_test(
    name = "dns_impl_test",
    srcs = ["dns_impl_test.cc"],
    args = [
        # Used in createDnsResolver to force creation of DnsResolverImpl when running test on macOS.
        "--runtime-feature-disable-for-tests=envoy.restart_features.use_apple_api_for_dns_lookups",
    ],
    # TODO(envoyproxy/windows-dev): Under winsock2 this is behaving unusually for windows, even as
    # 127.0.0.1 and ::1 are explicitly added to `c:\windows\system32\drivers\etc\hosts` ... see:
    # https://gist.github.com/wrowe/24fe5b93b58bb444bce7ecc134905395
    tags = ["fails_on_clang_cl"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/network:address_interface",
        "//include/envoy/network:dns_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:address_lib",
        "//source/common/network:dns_lib",
        "//source/common/network:filter_lib",
        "//source/common/network:listen_socket_lib",
        "//source/common/stats:stats_lib",
        "//source/common/stream_info:stream_info_lib",
        "//test/mocks/network:network_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
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
        "//source/extensions/filters/network/ratelimit:ratelimit_lib",
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
        "@envoy_api//envoy/extensions/filters/network/ratelimit/v3:pkg_cc_proto",
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
    tags = ["skip_on_windows"],
    deps = [
        ":udp_listener_impl_test_base_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:address_lib",
        "//source/common/network:listener_lib",
        "//source/common/network:socket_option_lib",
        "//source/common/network:udp_packet_writer_handler_lib",
        "//source/common/network:utility_lib",
        "//source/common/stats:stats_lib",
        "//source/extensions/quic_listeners/quiche:udp_gso_batch_writer_lib",
        "//test/common/network:listener_impl_test_base_lib",
        "//test/mocks/network:network_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
        "//test/test_common:utility_lib",
        "@com_googlesource_quiche//:quic_test_tools_mock_syscall_wrapper_lib",
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
    srcs = ["socket_option_test.h"],
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
        "//test/mocks/network:network_mocks",
        "//test/test_common:environment_lib",
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
    ],
)

envoy_cc_test(
    name = "io_socket_handle_impl_integration_test",
    srcs = ["io_socket_handle_impl_integration_test.cc"],
    tags = ["requires-network"],
    deps = [
        "//source/common/common:utility_lib",
        "//source/common/network:address_lib",
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
    name = "filter_matcher_test",
    srcs = ["filter_matcher_test.cc"],
    deps = [
        "//source/common/network:address_lib",
        "//source/common/network:filter_matcher_lib",
        "//test/mocks/network:network_mocks",
    ],
)
