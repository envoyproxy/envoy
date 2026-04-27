load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "dynamic_modules_cert_selector_test",
    srcs = ["dynamic_modules_cert_selector_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:cert_selector_config_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:cert_selector_context_index",
        "//test/extensions/dynamic_modules/test_data/c:cert_selector_no_op",
        "//test/extensions/dynamic_modules/test_data/c:no_op",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/transport_sockets/tls/cert_selectors/dynamic_modules:config",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/transport_sockets/tls/cert_selectors/dynamic_modules/v3:pkg_cc_proto",
    ],
)
