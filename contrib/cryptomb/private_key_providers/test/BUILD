load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_contrib_package",
)
load(
    "//contrib:all_contrib_extensions.bzl",
    "envoy_contrib_linux_x86_64_constraints",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test_library(
    name = "test_fake_factory",
    srcs = [
        "fake_factory.cc",
    ],
    hdrs = [
        "fake_factory.h",
    ],
    external_deps = ["ssl"],
    # This makes the test targets dependent on this target only run on the desired platform.
    # The actual feature is dependent on hardware but this mock library can run on other platforms.
    target_compatible_with = envoy_contrib_linux_x86_64_constraints(),
    deps = [
        "//contrib/cryptomb/private_key_providers/source:cryptomb_private_key_provider_lib",
        "//contrib/cryptomb/private_key_providers/source:ipp_crypto_wrapper_lib",
        "//envoy/api:api_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/server:transport_socket_config_interface",
        "//envoy/ssl/private_key:private_key_config_interface",
        "//envoy/ssl/private_key:private_key_interface",
        "//source/common/config:datasource_lib",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//contrib/envoy/extensions/private_key_providers/cryptomb/v3alpha:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = [
        "config_test.cc",
    ],
    data = [
        "//contrib/cryptomb/private_key_providers/test/test_data:certs",
    ],
    deps = [
        ":test_fake_factory",
        "//source/common/common:random_generator_lib",
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
        "//contrib/cryptomb/private_key_providers/test/test_data:certs",
    ],
    deps = [
        ":test_fake_factory",
        "//source/extensions/transport_sockets/tls/private_key:private_key_manager_lib",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)
