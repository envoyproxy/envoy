load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_dyn_module_test",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_dyn_module_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_dyn_module_test(
    name = "dynamic_modules_handshaker_test",
    srcs = ["dynamic_modules_handshaker_test.cc"],
    data = [
        "//test/common/tls/test_data:certs",
        "//test/extensions/dynamic_modules/test_data/c:no_op",
        "//test/extensions/dynamic_modules/test_data/c:tls_handshaker_bad_result",
        "//test/extensions/dynamic_modules/test_data/c:tls_handshaker_config_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:tls_handshaker_configure_fail",
        "//test/extensions/dynamic_modules/test_data/c:tls_handshaker_custom",
        "//test/extensions/dynamic_modules/test_data/c:tls_handshaker_handshake_only",
        "//test/extensions/dynamic_modules/test_data/c:tls_handshaker_manual",
        "//test/extensions/dynamic_modules/test_data/c:tls_handshaker_manual_fail",
        "//test/extensions/dynamic_modules/test_data/c:tls_handshaker_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:tls_handshaker_new_without_destroy",
        "//test/extensions/dynamic_modules/test_data/c:tls_handshaker_no_config_destroy",
        "//test/extensions/dynamic_modules/test_data/c:tls_handshaker_no_op",
        "//test/extensions/dynamic_modules/test_data/c:tls_handshaker_provides_certificates",
    ],
    rbe_pool = "linux_x64_small",
    deps = [
        "//envoy/ssl:handshaker_interface",
        "//source/common/tls:ssl_handshaker_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/transport_sockets/tls/handshakers/dynamic_modules:config",
        "//test/common/tls:ssl_certs_test_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/transport_sockets/tls/handshakers/dynamic_modules/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_dyn_module_test(
    name = "integration_test",
    size = "large",
    srcs = ["integration_test.cc"],
    data = [
        "//test/config/integration/certs",
        "//test/extensions/dynamic_modules/test_data/c:tls_handshaker_custom",
        "//test/extensions/dynamic_modules/test_data/c:tls_handshaker_no_op",
    ],
    extension_names = ["envoy.tls.handshakers.dynamic_modules"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/common/tls:context_lib",
        "//source/extensions/transport_sockets/tls:config",
        "//source/extensions/transport_sockets/tls/handshakers/dynamic_modules:config",
        "//test/common/tls/integration:ssl_integration_test_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/transport_sockets/tls/handshakers/dynamic_modules/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)
