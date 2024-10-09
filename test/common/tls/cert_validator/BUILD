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
        "//test/common/tls/test_data:certs",
    ],
    rbe_pool = "2core",
    deps = [
        "//source/common/tls/cert_validator:cert_validator_lib",
        "//test/common/tls:ssl_test_utils",
        "//test/common/tls/cert_validator:test_common",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:test_runtime_lib",
    ],
)

envoy_cc_test(
    name = "factory_test",
    srcs = [
        "factory_test.cc",
    ],
    rbe_pool = "2core",
    deps = [
        "//source/common/tls/cert_validator:cert_validator_lib",
        "//test/common/tls/cert_validator:test_common",
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
    rbe_pool = "2core",
    deps = [
        "//source/common/protobuf:utility_lib",
        "//source/common/tls/cert_validator:cert_validator_lib",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "default_validator_integration_test_lib",
    hdrs = ["default_validator_integration_test.h"],
)

envoy_cc_test(
    name = "cert_validator_integration_test",
    size = "large",
    srcs = [
        "default_validator_integration_test.cc",
    ],
    data = [
        "//test/common/tls/test_data:certs",
        "//test/config/integration/certs",
    ],
    rbe_pool = "2core",
    deps = [
        ":default_validator_integration_test_lib",
        "//test/integration:http_integration_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test_library(
    name = "timed_cert_validator",
    srcs = ["timed_cert_validator.cc"],
    hdrs = [
        "timed_cert_validator.h",
    ],
    deps = [
        "//source/common/tls/cert_validator:cert_validator_lib",
    ],
)
