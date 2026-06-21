load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "quic_sni_input_test",
    srcs = ["quic_sni_input_test.cc"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//source/common/network/matching:data_impl_lib",
        "//source/extensions/matching/udp/quic_sni:config",
        "//test/test_common:utility_lib",
    ],
)
