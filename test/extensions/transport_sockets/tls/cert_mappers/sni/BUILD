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
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = [],
    deps = [
        "//source/extensions/transport_sockets/tls/cert_mappers/sni:config",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/test_common:status_utility_lib",
        "@envoy_api//envoy/extensions/transport_sockets/tls/cert_mappers/sni/v3:pkg_cc_proto",
    ],
)
