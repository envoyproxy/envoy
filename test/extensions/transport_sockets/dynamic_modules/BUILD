load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:no_op",
        "//test/extensions/dynamic_modules/test_data/rust:transport_socket_integration_test",
    ],
    extension_names = ["envoy.transport_sockets.dynamic_modules"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/network:io_socket_error_lib",
        "//source/common/network:utility_lib",
        "//source/extensions/dynamic_modules:abi_impl",
        "//source/extensions/transport_sockets/dynamic_modules:abi_impl",
        "//source/extensions/transport_sockets/dynamic_modules:config",
        "//source/extensions/transport_sockets/dynamic_modules:transport_socket_lib",
        "//test/mocks/network:io_handle_mocks",
        "//test/mocks/network:transport_socket_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/transport_sockets/dynamic_modules/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "integration_test",
    size = "large",
    srcs = ["integration_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/rust:transport_socket_integration_test",
    ],
    extension_names = ["envoy.transport_sockets.dynamic_modules"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/dynamic_modules:abi_impl",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//source/extensions/transport_sockets/dynamic_modules:config",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//test/integration:integration_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/dynamic_modules/v3:pkg_cc_proto",
    ],
)
