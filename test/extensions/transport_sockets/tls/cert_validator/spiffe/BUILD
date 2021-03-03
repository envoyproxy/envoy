load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "spiffe_validator_test",
    srcs = [
        "spiffe_validator_test.cc",
    ],
    data = [
        "//test/extensions/transport_sockets/tls/test_data:certs",
    ],
    deps = [
        "//source/extensions/transport_sockets/tls/cert_validator/spiffe:config",
        "//test/extensions/transport_sockets/tls:ssl_test_utils",
        "//test/extensions/transport_sockets/tls/cert_validator:util",
        "//test/test_common:environment_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
    ],
)
