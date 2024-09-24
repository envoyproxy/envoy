load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "spiffe_validator_test",
    srcs = [
        "spiffe_validator_test.cc",
    ],
    data = [
        "//test/common/tls/test_data:certs",
    ],
    extension_names = ["envoy.tls.cert_validator.spiffe"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/transport_sockets/tls/cert_validator/spiffe:config",
        "//test/common/tls:ssl_test_utils",
        "//test/common/tls/cert_validator:test_common",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test_library(
    name = "spiffe_validator_integration_test_lib",
    hdrs = ["spiffe_validator_integration_test.h"],
)

envoy_extension_cc_test(
    name = "spiffe_validator_integration_test",
    size = "large",
    srcs = ["spiffe_validator_integration_test.cc"],
    data = [
        "//test/common/tls/test_data:certs",
        "//test/config/integration/certs",
    ],
    extension_names = ["envoy.tls.cert_validator.spiffe"],
    rbe_pool = "2core",
    # Broken until bazel 5.0.0 fix to shorten resulting paths for SymInitialize() failure
    tags = ["skip_on_windows"],
    deps = [
        ":spiffe_validator_integration_test_lib",
        "//source/extensions/transport_sockets/tls/cert_validator/spiffe:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
    ],
)
