load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

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
