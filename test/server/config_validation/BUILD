licenses(["notice"])  # Apache 2

package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_test")

envoy_cc_test(
    name = "server_test",
    srcs = ["server_test.cc"],
    data = [
        "//configs:google_com_proxy.json",
        "//test/config/integration:lightstep_access_token",
        "//test/config/integration:server.json",
    ],
    deps = [
        "//source/server/config_validation:hot_restart_lib",
        "//source/server/config_validation:server_lib",
        "//test/integration:integration_lib",
        "//test/mocks/server:server_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)
