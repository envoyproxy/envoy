load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    data = [
        "//contrib/golang/filters/network/test/test_data:filter.so",
    ],
    deps = [
        "//contrib/golang/filters/network/source:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "filter_test",
    srcs = ["filter_test.cc"],
    deps = [
        "//contrib/golang/common/dso/test:dso_mocks",
        "//contrib/golang/filters/network/source:golang",
        "//test/mocks/api:api_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "upstream_test",
    srcs = ["upstream_test.cc"],
    deps = [
        "//contrib/golang/common/dso/test:dso_mocks",
        "//contrib/golang/filters/network/source:upstream",
        "//source/common/network:filter_state_dst_address_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
    ],
)
