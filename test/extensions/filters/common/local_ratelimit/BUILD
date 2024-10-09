load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "local_ratelimit_test",
    srcs = ["local_ratelimit_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/singleton:manager_impl_lib",
        "//source/extensions/filters/common/local_ratelimit:local_ratelimit_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/mocks/upstream:cluster_priority_set_mocks",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
    ],
)
