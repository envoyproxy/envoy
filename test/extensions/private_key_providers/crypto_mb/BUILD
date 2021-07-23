load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "test_fake_factory",
    srcs = [
        "fake_factory.cc",
    ],
    hdrs = [
        "fake_factory.h",
    ],
    external_deps = ["ssl"],
    deps = [
        "//envoy/api:api_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/server:transport_socket_config_interface",
        "//envoy/ssl/private_key:private_key_config_interface",
        "//envoy/ssl/private_key:private_key_interface",
        "//source/common/config:datasource_lib",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/private_key_providers/cryptomb:config",
        "@envoy_api//envoy/extensions/private_key_providers/cryptomb/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = [
        "config_test.cc",
    ],
    data = [
        "//test/extensions/private_key_providers/crypto_mb/test_data:certs",
    ],
    deps = [
        ":test_fake_factory",
        "//source/common/common:random_generator_lib",
        "//source/extensions/private_key_providers/cryptomb:config",
        "//source/extensions/transport_sockets/tls/private_key:private_key_manager_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "ops_test",
    srcs = [
        "ops_test.cc",
    ],
    data = [
        "//test/extensions/private_key_providers/crypto_mb/test_data:certs",
    ],
    deps = [
        ":test_fake_factory",
        "//source/extensions/private_key_providers/cryptomb:config",
        "//source/extensions/transport_sockets/tls/private_key:private_key_manager_lib",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)
