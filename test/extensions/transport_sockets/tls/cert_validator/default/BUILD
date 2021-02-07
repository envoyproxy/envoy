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
        "//source/extensions/transport_sockets/tls/cert_validator/default:config",
        "//test/extensions/transport_sockets/tls:ssl_test_utils",
        "//test/extensions/transport_sockets/tls/cert_validator:util",
        "//test/test_common:environment_lib",
        "//test/test_common:test_runtime_lib",
    ],
)
