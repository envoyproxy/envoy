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
    rbe_pool = "2core",
    deps = [
        "//source/common/http/matching:data_impl_lib",
        "//source/common/network:address_lib",
        "//source/common/network:socket_lib",
        "//source/common/ssl/matching:inputs_lib",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
    ],
)

envoy_cc_test(
    name = "inputs_integration_test",
    srcs = ["inputs_integration_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/http/matching:data_impl_lib",
        "//source/common/network/matching:data_impl_lib",
        "//source/common/ssl/matching:inputs_lib",
        "//test/common/matcher:test_utility_lib",
        "//test/mocks/matcher:matcher_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:test_time_lib",
    ],
)
