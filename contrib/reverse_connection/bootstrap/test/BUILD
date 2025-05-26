load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "reverse_conn_registry_test",
    srcs = ["reverse_conn_registry_test.cc"],
    deps = [
        "//contrib/reverse_connection/bootstrap/source:reverse_conn_global_registry",
        "//test/mocks/event:event_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
    ],
)

envoy_cc_test(
    name = "reverse_conn_initiator_test",
    srcs = ["reverse_connection_initiator_test.cc"],
    deps = [
        ":rev_conn_mocks",
        "//source/common/network:address_lib",
        "//source/common/stats:isolated_store_lib",
        "//test/common/upstream:utility_lib",
        "//test/mocks/stats:stats_mocks",
    ],
)

envoy_cc_mock(
    name = "rev_conn_mocks",
    hdrs = ["mocks.h"],
    deps = [
        "//contrib/reverse_connection/bootstrap/source:reverse_conn_global_registry",
        "//test/mocks/event:event_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:network_utility_lib",
    ],
)
