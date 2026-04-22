load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "filter_test",
    srcs = ["filter_test.cc"],
    deps = [
        "//contrib/istio/filters/http/peer_metadata/source:config",
        "//source/common/network:address_lib",
        "//test/common/stream_info:test_util",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    deps = [
        "//contrib/istio/filters/http/peer_metadata/source:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)
