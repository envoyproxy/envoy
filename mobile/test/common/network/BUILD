load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "configurator_test",
    srcs = ["configurator_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/common/network:configurator_lib",
        "@envoy//test/extensions/common/dynamic_forward_proxy:mocks",
    ],
)

envoy_cc_test(
    name = "src_addr_socket_option_impl_test",
    srcs = ["src_addr_socket_option_impl_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/common/network:src_addr_socket_option_lib",
        "@envoy//test/mocks/network:network_mocks",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "synthetic_address_impl_test",
    srcs = ["synthetic_address_impl_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/common/network:synthetic_address_lib",
    ],
)
