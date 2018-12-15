licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "original_src_test",
    srcs = ["original_src_test.cc"],
    extension_name = "envoy.filters.network.original_src",
    deps = [
        "//source/extensions/filters/network/original_src:original_src_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/network:network_mocks",
    ],
)

envoy_extension_cc_test(
    name = "original_src_socket_option_test",
    srcs = ["original_src_socket_option_test.cc"],
    extension_name = "envoy.filters.network.original_src",
    deps = [
        "//source/common/network:address_lib",
        "//source/extensions/filters/network/original_src:original_src_socket_option_lib",
        "//test/mocks:common_lib",
        "//test/mocks/network:network_mocks",
    ],
)
