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
    deps = [
        "//source/extensions/filters/common/local_ratelimit:local_ratelimit_lib",
        "//test/mocks/event:event_mocks",
    ],
)
