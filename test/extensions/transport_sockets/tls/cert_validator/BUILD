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
        "//test/test_common:environment_lib",
        "//test/test_common:test_runtime_lib",
    ],
)

envoy_cc_test(
    name = "spiffe_validator_test",
    srcs = [
        "spiffe_validator_test.cc",
    ],
    data = [
        "//test/extensions/transport_sockets/tls/test_data:certs",
    ],
    deps = [
        ":util",
        "//source/extensions/transport_sockets/tls/cert_validator:cert_validator_lib",
        "//test/extensions/transport_sockets/tls:ssl_test_utils",
        "//test/test_common:environment_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "factory_test",
    srcs = [
        "factory_test.cc",
    ],
    deps = [
        ":util",
        "//source/extensions/transport_sockets/tls/cert_validator:cert_validator_lib",
    ],
)

envoy_cc_test_library(
    name = "util",
    hdrs = ["util.h"],
    deps = [
        "//include/envoy/ssl:context_config_interface",
        "//include/envoy/ssl:ssl_socket_extended_info_interface",
        "//source/common/common:macros",
        "//test/test_common:utility_lib",
    ],
)
