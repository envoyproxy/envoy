licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.stat_sinks.dog_statsd",
    deps = [
        "//include/envoy/registry",
        "//source/common/config:well_known_names",
        "//source/common/protobuf:utility_lib",
<<<<<<< HEAD:test/server/config/stats/BUILD
        "//source/common/stats:hystrix_lib",
        "//source/common/stats:statsd_lib",
        "//source/server/config/stats:dog_statsd_lib",
        "//source/server/config/stats:hystrix_lib",
        "//source/server/config/stats:statsd_lib",
=======
        "//source/extensions/stat_sinks/dog_statsd:config",
>>>>>>> aa61c6c34f7bd4c1448649686b5bd7511aaa8d51:test/extensions/stats_sinks/dog_statsd/BUILD
        "//test/mocks/server:server_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v2:bootstrap_cc",
    ],
)
