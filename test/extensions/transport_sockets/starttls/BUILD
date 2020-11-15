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
    name = "starttls_socket_tests",
    srcs = [
        "starttls_socket_test.cc",
    ],
    extension_name = "envoy.transport_sockets.starttls",
    deps = [
        "//source/common/network:transport_socket_options_lib",
        "//source/extensions/transport_sockets/starttls:config",
        "//test/mocks/network:network_mocks",
        "@envoy_api//envoy/extensions/transport_sockets/starttls/v3:pkg_cc_proto",
    ],
)
