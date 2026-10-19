load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_dyn_module_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_dyn_module_test(
    name = "tls_certificate_selector_test",
    srcs = ["tls_certificate_selector_test.cc"],
    data = [
        "//test/config/integration/certs",
        "//test/extensions/dynamic_modules/test_data/c:no_op",
        "//test/extensions/dynamic_modules/test_data/c:tls_certificate_selector_by_server_name",
        "//test/extensions/dynamic_modules/test_data/c:tls_certificate_selector_dynamic",
        "//test/extensions/dynamic_modules/test_data/c:tls_certificate_selector_dynamic_without_hooks",
        "//test/extensions/dynamic_modules/test_data/c:tls_certificate_selector_missing_config_destroy",
        "//test/extensions/dynamic_modules/test_data/c:tls_certificate_selector_missing_destroy",
        "//test/extensions/dynamic_modules/test_data/c:tls_certificate_selector_missing_new",
        "//test/extensions/dynamic_modules/test_data/c:tls_certificate_selector_missing_select",
        "//test/extensions/dynamic_modules/test_data/c:tls_certificate_selector_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:tls_certificate_selector_out_of_range",
    ],
    extension_names = ["envoy.tls.certificate_selectors.dynamic_modules"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/common/config:utility_lib",
        "//source/common/router:string_accessor_lib",
        "//source/common/tls:context_config_lib",
        "//source/common/tls:context_lib",
        "//source/common/tls:server_context_config_lib",
        "//source/common/tls:server_context_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/transport_sockets/tls/cert_selectors/common:async_context_lib",
        "//source/extensions/transport_sockets/tls/cert_selectors/dynamic_modules:config",
        "//source/extensions/transport_sockets/tls/cert_selectors/dynamic_modules:tls_certificate_selector_lib",
        "//test/common/tls:ssl_certs_test_lib",
        "//test/common/tls:ssl_test_utils",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/event:event_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:status_utility_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/transport_sockets/tls/cert_selectors/dynamic_modules/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_dyn_module_test(
    name = "integration_test",
    size = "large",
    srcs = ["integration_test.cc"],
    data = [
        "//test/config/integration/certs",
        "//test/extensions/dynamic_modules/test_data/rust:tls_certificate_selector_integration_test",
    ],
    extension_names = ["envoy.tls.certificate_selectors.dynamic_modules"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/common/tls:ssl_handshaker_lib",
        "//source/extensions/filters/http/set_filter_state:config",
        "//source/extensions/transport_sockets/tls:config",
        "//source/extensions/transport_sockets/tls/cert_selectors/dynamic_modules:config",
        "//test/common/tls/integration:ssl_integration_test_lib",
        "//test/config/integration/certs:hashes",
        "//test/integration:http_integration_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@abseil-cpp//absl/strings",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)
