load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "thread_local_impl_test",
    srcs = ["thread_local_impl_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/api:api_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/common/thread_local:thread_local_lib",
        "//test/mocks/event:event_mocks",
        "//test/test_common:test_runtime_lib",
    ],
)
