load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "dynamic_modules_cert_validator_integration_test",
    size = "large",
    srcs = ["dynamic_modules_cert_validator_integration_test.cc"],
    data = [
        "//test/common/tls/test_data:certs",
        "//test/config/integration/certs",
        "//test/extensions/dynamic_modules/test_data/c:cert_validator_filter_state",
        "//test/extensions/dynamic_modules/test_data/c:cert_validator_no_op",
        "//test/extensions/dynamic_modules/test_data/go:cert_validator_test",
        "//test/extensions/dynamic_modules/test_data/rust:cert_validator_test",
    ],
    env = {"GODEBUG": "cgocheck=0"},
    rbe_pool = "6gig",
    deps = [
        # See //test/extensions/dynamic_modules/http:filter_test for why every test
        # binary that loads a Go test_data .so depends on all_abi_impls.
        "//source/extensions/dynamic_modules:all_abi_impls",
        "//source/extensions/transport_sockets/tls/cert_validator/dynamic_modules:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "dynamic_modules_cert_validator_test",
    srcs = ["dynamic_modules_cert_validator_test.cc"],
    data = [
        "//test/common/tls/test_data:certs",
        "//test/extensions/dynamic_modules/test_data/c:cert_validator_config_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:cert_validator_empty_digest",
        "//test/extensions/dynamic_modules/test_data/c:cert_validator_fail",
        "//test/extensions/dynamic_modules/test_data/c:cert_validator_filter_state",
        "//test/extensions/dynamic_modules/test_data/c:cert_validator_missing_config_destroy",
        "//test/extensions/dynamic_modules/test_data/c:cert_validator_missing_do_verify",
        "//test/extensions/dynamic_modules/test_data/c:cert_validator_missing_get_verify_mode",
        "//test/extensions/dynamic_modules/test_data/c:cert_validator_missing_update_digest",
        "//test/extensions/dynamic_modules/test_data/c:cert_validator_no_client_cert",
        "//test/extensions/dynamic_modules/test_data/c:cert_validator_no_op",
        "//test/extensions/dynamic_modules/test_data/c:cert_validator_not_validated",
        "//test/extensions/dynamic_modules/test_data/c:cert_validator_unknown_status",
        "//test/extensions/dynamic_modules/test_data/c:no_op",
        "//test/extensions/dynamic_modules/test_data/go:cert_validator_test",
        "//test/extensions/dynamic_modules/test_data/rust:cert_validator_test",
    ],
    env = {"GODEBUG": "cgocheck=0"},
    rbe_pool = "6gig",
    deps = [
        "//source/common/router:string_accessor_lib",
        # See //test/extensions/dynamic_modules/http:filter_test for why every test
        # binary that loads a Go test_data .so depends on all_abi_impls.
        "//source/extensions/dynamic_modules:all_abi_impls",
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
