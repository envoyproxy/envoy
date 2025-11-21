load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

# Stub implementations of ABI functions for testing without a real dynamic module
envoy_cc_library(
    name = "abi_stubs",
    srcs = ["abi_stubs.cc"],
    deps = [
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
    ],
    alwayslink = 1,
)

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    rbe_pool = "6gig",
    deps = [
        ":abi_stubs",
        "//source/extensions/transport_sockets/dynamic_modules:config",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "transport_socket_test",
    srcs = ["transport_socket_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/rust:transport_socket_integration_test",
    ],
    deps = [
        ":abi_stubs",
        "//source/common/buffer:buffer_lib",
        "//source/extensions/transport_sockets/dynamic_modules:config",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "integration_test",
    srcs = ["integration_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/rust:abi_coverage_transport_socket",
        "//test/extensions/dynamic_modules/test_data/rust:rustls_tls_transport_socket",
        "//test/extensions/dynamic_modules/test_data/rust:transport_socket_integration_test",
    ],
    rbe_pool = "6gig",
    # Sanitizers report leaks from internal rustls crypto library allocations that persist
    # beyond dlclose().
    tags = ["no_san"],
    deps = [
        "//source/extensions/transport_sockets/dynamic_modules:config",
        "//test/integration:http_integration_lib",
        "@com_google_protobuf//:protobuf",
        "@envoy_api//envoy/extensions/transport_sockets/dynamic_modules/v3:pkg_cc_proto",
    ],
)
