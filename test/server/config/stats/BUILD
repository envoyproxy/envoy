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
        "//include/envoy/registry",
        "//source/common/config:well_known_names",
        "//source/common/protobuf:utility_lib",
        "//source/common/stats:statsd_lib",
        "//source/server/config/stats:dog_statsd_lib",
        "//source/server/config/stats:statsd_lib",
        "//test/mocks/server:server_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v2:bootstrap_cc",
    ],
)
