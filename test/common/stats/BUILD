licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "stats_impl_test",
    srcs = ["stats_impl_test.cc"],
    deps = [
        "//source/common/stats:stats_lib",
        "//source/server:hot_restart_lib",
        "//source/server:options_lib",
        "//test/mocks/server:server_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/metrics/v2:stats_cc",
    ],
)

envoy_cc_test(
    name = "thread_local_store_test",
    srcs = ["thread_local_store_test.cc"],
    deps = [
        "//source/common/stats:stats_lib",
        "//source/common/stats:thread_local_store_lib",
        "//source/server:hot_restart_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/test_common:utility_lib",
    ],
)
