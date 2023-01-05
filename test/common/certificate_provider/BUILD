load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_proto_library(
    name = "test_certificate_provider_proto",
    srcs = ["test_certificate_provider.proto"],
)

envoy_cc_test(
    name = "cert_provider_manager_impl_test",
    srcs = ["certificate_provider_manager_impl_test.cc"],
    data = [],
    deps = [
        "test_certificate_provider_proto_cc_proto",
        "//test/mocks/server:transport_socket_factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
    ],
)
