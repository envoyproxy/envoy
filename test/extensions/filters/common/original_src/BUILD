load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "original_src_socket_option_test",
    srcs = ["original_src_socket_option_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/network:address_lib",
        "//source/extensions/filters/common/original_src:original_src_socket_option_lib",
        "//test/mocks:common_lib",
        "//test/mocks/network:network_mocks",
        "//test/test_common:printers_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)
