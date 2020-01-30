licenses(["notice"])  # Apache 2

load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_package")

envoy_package()

envoy_cc_test(
    name = "lock_guard_test",
    srcs = ["lock_guard_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/common/thread:lock_guard_lib",
        "@envoy//source/common/common:thread_lib",
    ],
)
