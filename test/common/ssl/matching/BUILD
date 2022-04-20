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
        "//source/common/ssl/matching:inputs_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "@envoy_api//envoy/extensions/matching/common_inputs/ssl/v3:pkg_cc_proto",
    ],
)
