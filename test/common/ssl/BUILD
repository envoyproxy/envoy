licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "ssl_socket_test",
    srcs = [
        "ssl_certs_test.h",
        "ssl_socket_test.cc",
    ],
    data = [
        "gen_unittest_certs.sh",
        "//test/common/ssl/test_data:certs",
    ],
    external_deps = ["ssl"],
    deps = [
        "//include/envoy/network:transport_socket_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:empty_string",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/network:listen_socket_lib",
        "//source/common/network:utility_lib",
        "//source/common/ssl:context_config_lib",
        "//source/common/ssl:context_lib",
        "//source/common/ssl:ssl_socket_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/common/stats:stats_lib",
        "//source/extensions/filters/listener/tls_inspector:tls_inspector_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:test_time_lib",
        "//test/test_common:utility_lib",
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
        "//test/common/ssl/test_data:certs",
    ],
    deps = [
        ":ssl_test_utils",
        "//source/common/json:json_loader_lib",
        "//source/common/ssl:context_config_lib",
        "//source/common/ssl:context_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/common/stats:stats_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/secret:secret_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:environment_lib",
    ],
)

envoy_cc_test(
    name = "utility_test",
    srcs = [
        "utility_test.cc",
    ],
    data = [
        "gen_unittest_certs.sh",
        "//test/common/ssl/test_data:certs",
    ],
    external_deps = ["ssl"],
    deps = [
        ":ssl_test_utils",
        "//source/common/ssl:utility_lib",
        "//test/test_common:environment_lib",
    ],
)

envoy_cc_test_library(
    name = "ssl_test_utils",
    srcs = [
        "ssl_test_utility.h",
    ],
    deps = [
        "//source/common/ssl:utility_lib",
        "//test/test_common:environment_lib",
    ],
)
