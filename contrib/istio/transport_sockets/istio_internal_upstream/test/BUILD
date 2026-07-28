load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "istio_internal_upstream_test",
    srcs = ["istio_internal_upstream_test.cc"],
    deps = [
        "//contrib/istio/transport_sockets/istio_internal_upstream/source:istio_internal_upstream",
        "//source/extensions/io_socket/user_space:io_handle_lib",
        "//test/mocks/network:io_handle_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/network:transport_socket_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
