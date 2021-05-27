load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_mock(
    name = "ssl_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//include/envoy/ssl:certificate_validation_context_config_interface_no_include_prefix",
        "//include/envoy/ssl:connection_interface_no_include_prefix",
        "//include/envoy/ssl:context_config_interface_no_include_prefix",
        "//include/envoy/ssl:context_interface_no_include_prefix",
        "//include/envoy/ssl:context_manager_interface_no_include_prefix",
        "//include/envoy/stats:stats_interface_no_include_prefix",
        "//test/mocks/secret:secret_mocks",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)
