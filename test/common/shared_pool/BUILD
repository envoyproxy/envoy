load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "shared_pool_test",
    srcs = ["shared_pool_test.cc"],
    deps = [
        "//source/common/event:timer_lib",
        "//source/common/shared_pool:shared_pool_lib",
        "//test/mocks/event:event_mocks",
        "//test/test_common:thread_factory_for_test_lib",
    ],
)
