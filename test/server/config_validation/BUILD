licenses(["notice"])  # Apache 2

load("//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_package")

envoy_package()

envoy_cc_test(
    name = "server_test",
    srcs = ["server_test.cc"],
    data = [
        "//configs:example_configs",
        "//test/config_test:example_configs_test_setup.sh",
    ],
    deps = [
        "//source/server/config_validation:server_lib",
        "//test/integration:integration_lib",
        "//test/mocks/server:server_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)
