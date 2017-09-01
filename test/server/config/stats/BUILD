licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    deps = [
        "//source/common/stats:statsd_lib",
        "//source/server/config/stats:statsd_lib",
        "//test/mocks/server:server_mocks",
        "//test/test_common:utility_lib",
    ],
)
