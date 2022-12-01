load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
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
        "//test/extensions/transport_sockets/tls/cert_validator:test_common",
        "//test/test_common:environment_lib",
        "//test/test_common:test_runtime_lib",
    ],
)

envoy_cc_test(
    name = "factory_test",
    srcs = [
        "factory_test.cc",
    ],
    deps = [
        "//source/extensions/transport_sockets/tls/cert_validator:cert_validator_lib",
        "//test/extensions/transport_sockets/tls/cert_validator:test_common",
    ],
)

envoy_cc_test_library(
    name = "test_common",
    hdrs = ["test_common.h"],
    deps = [
        "//envoy/ssl:context_config_interface",
        "//envoy/ssl:ssl_socket_extended_info_interface",
        "//source/common/common:macros",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "san_matcher_test",
    srcs = [
        "san_matcher_test.cc",
    ],
    deps = [
        "//source/common/protobuf:utility_lib",
        "//source/extensions/transport_sockets/tls/cert_validator:cert_validator_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "cert_validator_integration_test",
    srcs = [
        "default_validator_integration_test.cc",
        "default_validator_integration_test.h",
    ],
    data = [
        "//test/config/integration/certs",
        "//test/extensions/transport_sockets/tls/test_data:certs",
    ],
    deps = [
        "//test/integration:http_integration_lib",
    ],
)

envoy_cc_test_library(
    name = "timed_cert_validator",
    srcs = ["timed_cert_validator.cc"],
    hdrs = [
        "timed_cert_validator.h",
    ],
    deps = [
        "//source/extensions/transport_sockets/tls/cert_validator:cert_validator_lib",
    ],
)
