load(
    "//bazel:envoy_build_system.bzl",
    "envoy_benchmark_test",
    "envoy_cc_benchmark_binary",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "ssl_certs_test_lib",
    hdrs = ["ssl_certs_test.h"],
)

envoy_cc_test(
    name = "tls_certificate_selector_test",
    size = "large",
    srcs = [
        "tls_certificate_selector_test.cc",
    ],
    data = [
        # TODO(mattklein123): We should consolidate all of our test certs in a single place as
        # right now we have a bunch of duplication which is confusing.
        "//test/config/integration/certs",
        "//test/common/tls/ocsp/test_data:certs",
        "//test/common/tls/test_data:certs",
    ],
    external_deps = ["ssl"],
    rbe_pool = "2core",
    deps = [
        ":ssl_certs_test_lib",
        ":test_private_key_method_provider_test_lib",
        "//envoy/network:transport_socket_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:empty_string",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/network:listen_socket_lib",
        "//source/common/network:transport_socket_options_lib",
        "//source/common/network:utility_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/common/stats:stats_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/common/tls:context_config_lib",
        "//source/common/tls:context_lib",
        "//source/common/tls:server_context_config_lib",
        "//source/common/tls:server_context_lib",
        "//source/common/tls:ssl_socket_lib",
        "//source/common/tls:utility_lib",
        "//source/common/tls/private_key:private_key_manager_lib",
        "//test/common/tls/cert_validator:timed_cert_validator",
        "//test/common/tls/test_data:cert_infos",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/init:init_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:io_handle_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "ssl_socket_test",
    size = "large",
    srcs = [
        "ssl_socket_test.cc",
    ],
    data = [
        # TODO(mattklein123): We should consolidate all of our test certs in a single place as
        # right now we have a bunch of duplication which is confusing.
        "//test/config/integration/certs",
        "//test/common/tls/ocsp/test_data:certs",
        "//test/common/tls/test_data:certs",
    ],
    external_deps = ["ssl"],
    rbe_pool = "2core",
    deps = [
        ":ssl_certs_test_lib",
        ":test_private_key_method_provider_test_lib",
        "//envoy/network:transport_socket_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:empty_string",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/network:listen_socket_lib",
        "//source/common/network:transport_socket_options_lib",
        "//source/common/network:utility_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/common/stats:stats_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/common/tls:context_config_lib",
        "//source/common/tls:context_lib",
        "//source/common/tls:server_context_config_lib",
        "//source/common/tls:server_context_lib",
        "//source/common/tls:ssl_socket_lib",
        "//source/common/tls:utility_lib",
        "//source/common/tls/private_key:private_key_manager_lib",
        "//test/common/tls/cert_validator:timed_cert_validator",
        "//test/common/tls/test_data:cert_infos",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/init:init_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:io_handle_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "context_impl_test",
    srcs = [
        "context_impl_test.cc",
    ],
    data = [
        "//test/common/tls/ocsp/test_data:certs",
        "//test/common/tls/test_data:certs",
    ],
    rbe_pool = "2core",
    deps = [
        "ssl_certs_test_lib",
        ":ssl_test_utils",
        "//source/common/common:base64_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/common/stats:stats_lib",
        "//source/common/tls:context_config_lib",
        "//source/common/tls:context_lib",
        "//source/common/tls:server_context_config_lib",
        "//source/common/tls:server_context_lib",
        "//test/common/tls/test_data:cert_infos",
        "//test/mocks/init:init_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/secret:secret_mocks",
        "//test/mocks/server:transport_socket_factory_context_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:simulated_time_system_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "io_handle_bio_test",
    srcs = ["io_handle_bio_test.cc"],
    external_deps = ["ssl"],
    rbe_pool = "2core",
    deps = [
        ":ssl_test_utils",
        "//source/common/tls:ssl_socket_lib",
        "//test/mocks/network:io_handle_mocks",
    ],
)

envoy_cc_test(
    name = "utility_test",
    srcs = [
        "utility_test.cc",
    ],
    data = [
        "//test/common/tls/test_data:certs",
    ],
    external_deps = ["ssl"],
    rbe_pool = "2core",
    deps = [
        ":ssl_test_utils",
        "//source/common/tls:utility_lib",
        "//test/common/tls/test_data:cert_infos",
        "//test/test_common:environment_lib",
        "//test/test_common:simulated_time_system_lib",
    ],
)

envoy_cc_test_library(
    name = "ssl_test_utils",
    hdrs = [
        "ssl_test_utility.h",
    ],
    deps = [
        "//source/common/tls:ssl_socket_lib",
        "//source/common/tls:utility_lib",
        "//test/test_common:environment_lib",
    ],
)

envoy_cc_test_library(
    name = "test_private_key_method_provider_test_lib",
    srcs = [
        "test_private_key_method_provider.cc",
    ],
    hdrs = [
        "test_private_key_method_provider.h",
    ],
    external_deps = ["ssl"],
    deps = [
        "//envoy/api:api_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/server:transport_socket_config_interface",
        "//envoy/ssl/private_key:private_key_config_interface",
        "//envoy/ssl/private_key:private_key_interface",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "handshaker_test",
    srcs = ["handshaker_test.cc"],
    data = [
        "//test/common/tls/test_data:certs",
    ],
    external_deps = ["ssl"],
    rbe_pool = "2core",
    deps = [
        ":ssl_socket_test",
        ":ssl_test_utils",
        "//source/common/stream_info:stream_info_lib",
        "//source/common/tls:server_context_lib",
        "//source/common/tls:ssl_handshaker_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/stats:stats_mocks",
    ],
)

envoy_cc_test(
    name = "handshaker_factory_test",
    srcs = ["handshaker_factory_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":ssl_test_utils",
        "//source/common/stream_info:stream_info_lib",
        "//source/common/tls:context_lib",
        "//source/common/tls:server_context_config_lib",
        "//source/common/tls:server_context_lib",
        "//source/common/tls:ssl_handshaker_lib",
        "//source/common/tls:ssl_socket_lib",
        "//source/server:process_context_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:registry_lib",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "server_context_impl_test",
    srcs = ["server_context_impl_test.cc"],
    deps = [
        "//source/common/tls:server_context_lib",
    ],
)

envoy_cc_benchmark_binary(
    name = "tls_throughput_benchmark",
    srcs = ["tls_throughput_benchmark.cc"],
    data = [
        "//test/common/tls/test_data:certs",
    ],
    external_deps = ["ssl"],
    rbe_pool = "2core",
    # Uses raw POSIX syscalls, does not build on Windows.
    tags = ["skip_on_windows"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "@com_github_google_benchmark//:benchmark",
    ],
)

envoy_benchmark_test(
    name = "tls_throughput_benchmark_test",
    benchmark_binary = "tls_throughput_benchmark",
    # Uses raw POSIX syscalls, does not build on Windows.
    tags = ["skip_on_windows"],
)
