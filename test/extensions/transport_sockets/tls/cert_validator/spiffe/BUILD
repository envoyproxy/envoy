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
    name = "spiffe_validator_test",
    srcs = [
        "spiffe_validator_test.cc",
    ],
    data = [
        "//test/extensions/transport_sockets/tls/test_data:certs",
    ],
    extension_names = ["envoy.tls.cert_validator.spiffe"],
    deps = [
        "//source/extensions/transport_sockets/tls/cert_validator/spiffe:config",
        "//test/extensions/transport_sockets/tls:ssl_test_utils",
        "//test/extensions/transport_sockets/tls/cert_validator:test_common",
        "//test/test_common:environment_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "spiffe_validator_integration_test",
    srcs = [
        "spiffe_validator_integration_test.cc",
        "spiffe_validator_integration_test.h",
    ],
    data = [
        "//test/config/integration/certs",
        "//test/extensions/transport_sockets/tls/test_data:certs",
    ],
    extension_names = ["envoy.tls.cert_validator.spiffe"],
    # Broken until bazel 5.0.0 fix to shorten resulting paths for SymInitialize() failure
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/transport_sockets/tls/cert_validator/spiffe:config",
        "//test/integration:http_integration_lib",
    ],
)
