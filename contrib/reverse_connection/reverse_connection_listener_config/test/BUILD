load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "active_reverse_conn_listener_test",
    srcs = ["active_reverse_connection_listener_test.cc"],
    deps = [
        "//contrib/reverse_connection/bootstrap/test:rev_conn_mocks",
        "//contrib/reverse_connection/reverse_connection_listener_config/source:active_reverse_connection_listener_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
    ],
)
