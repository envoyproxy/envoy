load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "dynamic_modules_cert_validator_test",
    srcs = ["dynamic_modules_cert_validator_test.cc"],
    data = [
        "//test/common/tls/test_data:certs",
        "//test/extensions/dynamic_modules/test_data/c:cert_validator_config_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:cert_validator_empty_digest",
        "//test/extensions/dynamic_modules/test_data/c:cert_validator_fail",
        "//test/extensions/dynamic_modules/test_data/c:cert_validator_filter_state",
        "//test/extensions/dynamic_modules/test_data/c:cert_validator_no_client_cert",
        "//test/extensions/dynamic_modules/test_data/c:cert_validator_no_op",
        "//test/extensions/dynamic_modules/test_data/c:cert_validator_not_validated",
        "//test/extensions/dynamic_modules/test_data/c:no_op",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/common/router:string_accessor_lib",
        "//source/extensions/transport_sockets/tls/cert_validator/dynamic_modules:config",
        "//test/common/tls:ssl_test_utils",
        "//test/common/tls/cert_validator:test_common",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)
