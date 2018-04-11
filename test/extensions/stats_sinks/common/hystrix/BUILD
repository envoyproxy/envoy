licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "hystrix_test",
    srcs = ["hystrix_test.cc"],
    deps = [
        "//source/common/stats:stats_lib",
        "//source/extensions/stat_sinks/common/hystrix:hystrix_lib",
        "//test/mocks/server:server_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)
