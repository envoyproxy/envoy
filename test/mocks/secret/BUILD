load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_mock(
    name = "secret_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//envoy/secret:secret_callbacks_interface",
        "//envoy/secret:secret_manager_interface",
        "//envoy/server:transport_socket_config_interface",
        "//envoy/ssl:tls_certificate_config_interface",
        "//source/common/secret:secret_provider_impl_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)
