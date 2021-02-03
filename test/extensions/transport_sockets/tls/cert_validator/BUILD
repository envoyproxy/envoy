load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "default_validator_test",
    srcs = [
        "default_validator_test.cc",
    ],
    data = [
        "//test/extensions/transport_sockets/tls/test_data:certs",
    ],
    deps = [
        "//source/extensions/transport_sockets/tls/cert_validator:cert_validator_lib",
        "//test/extensions/transport_sockets/tls:ssl_test_utils",
        "//test/test_common:environment_lib",
        "//test/test_common:test_runtime_lib",
    ],
)
