load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "transport_socket_test",
    srcs = ["transport_socket_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:no_op",
        "//test/extensions/dynamic_modules/test_data/c:transport_socket_no_op",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/transport_sockets/dynamic_modules:config",
        "//source/extensions/transport_sockets/dynamic_modules:transport_socket_config_lib",
        "//source/extensions/transport_sockets/dynamic_modules:transport_socket_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/network:network_mocks",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:no_op",
        "//test/extensions/dynamic_modules/test_data/c:transport_socket_no_op",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/transport_sockets/dynamic_modules:config",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/server:server_factory_context_mocks",
        "@envoy_api//envoy/extensions/transport_sockets/dynamic_modules/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "abi_impl_test",
    srcs = ["abi_impl_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:transport_socket_no_op",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/extensions/dynamic_modules:transport_socket_abi_lib",
        "//source/extensions/transport_sockets/dynamic_modules:transport_socket_config_lib",
        "//source/extensions/transport_sockets/dynamic_modules:transport_socket_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/network:io_handle_mocks",
        "//test/mocks/network:network_mocks",
    ],
)
