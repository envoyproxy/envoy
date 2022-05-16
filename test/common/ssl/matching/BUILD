load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "test_data_lib",
    hdrs = ["test_data.h"],
    deps = [
        "//test/mocks/ssl:ssl_mocks",
    ],
)

envoy_cc_test(
    name = "inputs_test",
    srcs = ["inputs_test.cc"],
    deps = [
        ":test_data_lib",
        "//source/common/ssl/matching:inputs_lib",
    ],
)

envoy_cc_test(
    name = "inputs_integration_test",
    srcs = ["inputs_integration_test.cc"],
    deps = [
        ":test_data_lib",
        "//source/common/ssl/matching:inputs_lib",
        "//test/common/matcher:test_utility_lib",
        "//test/mocks/matcher:matcher_mocks",
        "//test/mocks/server:factory_context_mocks",
    ],
)
