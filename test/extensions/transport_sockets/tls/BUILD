load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "ssl_socket_test",
    srcs = [
        "ssl_certs_test.h",
        "ssl_socket_test.cc",
    ],
    data = [
        "gen_unittest_certs.sh",
        # TODO(mattklein123): We should consolidate all of our test certs in a single place as
        # right now we have a bunch of duplication which is confusing.
        "//test/config/integration/certs",
        "//test/extensions/transport_sockets/tls/test_data:certs",
    ],
    external_deps = ["ssl"],
    shard_count = 4,
    # TODO(wrowe): Diagnose timeout error on Windows (skipped for the moment)
    tags = ["fails_on_windows"],
    deps = [
        ":test_private_key_method_provider_test_lib",
        "//include/envoy/network:transport_socket_interface",
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
        "//source/extensions/transport_sockets/tls:context_config_lib",
        "//source/extensions/transport_sockets/tls:context_lib",
        "//source/extensions/transport_sockets/tls:ssl_socket_lib",
        "//source/extensions/transport_sockets/tls:utility_lib",
        "//source/extensions/transport_sockets/tls/private_key:private_key_manager_lib",
        "//test/extensions/transport_sockets/tls/test_data:cert_infos",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/init:init_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "context_impl_test",
    srcs = [
        "context_impl_test.cc",
        "ssl_certs_test.h",
    ],
    data = [
        "gen_unittest_certs.sh",
        "//test/extensions/transport_sockets/tls/test_data:certs",
    ],
    # Fails intermittantly on local build
    tags = ["flaky_on_windows"],
    deps = [
        ":ssl_test_utils",
        "//source/common/json:json_loader_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/common/stats:stats_lib",
        "//source/extensions/transport_sockets/tls:context_config_lib",
        "//source/extensions/transport_sockets/tls:context_lib",
        "//test/extensions/transport_sockets/tls/test_data:cert_infos",
        "//test/mocks/init:init_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/secret:secret_mocks",
        "//test/mocks/server:transport_socket_factory_context_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "utility_test",
    srcs = [
        "utility_test.cc",
    ],
    data = [
        "gen_unittest_certs.sh",
        "//test/extensions/transport_sockets/tls/test_data:certs",
    ],
    external_deps = ["ssl"],
    deps = [
        ":ssl_test_utils",
        "//source/extensions/transport_sockets/tls:utility_lib",
        "//test/extensions/transport_sockets/tls/test_data:cert_infos",
        "//test/test_common:environment_lib",
        "//test/test_common:simulated_time_system_lib",
    ],
)

envoy_cc_test_library(
    name = "ssl_test_utils",
    srcs = [
        "ssl_test_utility.h",
    ],
    deps = [
        "//source/extensions/transport_sockets/tls:utility_lib",
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
        "//include/envoy/api:api_interface",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/server:transport_socket_config_interface",
        "//include/envoy/ssl/private_key:private_key_config_interface",
        "//include/envoy/ssl/private_key:private_key_interface",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)
