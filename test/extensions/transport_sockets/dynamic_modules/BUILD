load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/rust:transport_socket_noop",
        "//test/extensions/dynamic_modules/test_data/rust:transport_socket_passthrough",
    ],
    tags = [
        "cpu:3",
    ],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/network:transport_socket_options_lib",
        "//source/extensions/transport_sockets/dynamic_modules:config",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/network:transport_socket_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "@envoy_api//envoy/extensions/transport_sockets/dynamic_modules/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "abi_test",
    srcs = ["abi_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/rust:transport_socket_passthrough",
    ],
    tags = [
        "cpu:3",
    ],
    deps = [
        "//source/extensions/transport_sockets/dynamic_modules:abi_impl_lib",
        "//source/extensions/transport_sockets/dynamic_modules:config",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "@envoy_api//envoy/extensions/transport_sockets/dynamic_modules/v3:pkg_cc_proto",
    ],
)
