licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "thread_factory_singleton_test",
    srcs = ["thread_factory_singleton_test.cc"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/thread:thread_factory_singleton_lib",
    ],
)
