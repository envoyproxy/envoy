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
        "//source/extensions/health_checkers/redis_health_checker:config",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_cc_test(
    name = "redis_health_checker_test",
    srcs = ["redis_health_checker_test.cc"],
    deps = [
        "//source/extensions/health_checkers/redis_health_checker",
        "//test/common/upstream:utility_lib",
        "//test/extensions/filters/network/redis_proxy:redis_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)
