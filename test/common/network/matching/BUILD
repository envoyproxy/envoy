load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "inputs_test",
    srcs = ["inputs_test.cc"],
    deps = [
        "//source/common/network:address_lib",
        "//source/common/network/matching:data_impl_lib",
        "//source/common/network/matching:inputs_lib",
        "//test/mocks/network:network_mocks",
    ],
)
