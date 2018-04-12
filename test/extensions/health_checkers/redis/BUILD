licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "redis_test",
    srcs = ["redis_test.cc"],
    deps = [
        "//source/extensions/health_checkers/redis",
        "//source/extensions/health_checkers/redis:utility",
        "//test/common/upstream:utility_lib",
        "//test/extensions/filters/network/redis_proxy:redis_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    deps = [
        "//source/common/upstream:health_checker_lib",
        "//source/extensions/health_checkers/redis:config",
        "//test/common/upstream:utility_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)
