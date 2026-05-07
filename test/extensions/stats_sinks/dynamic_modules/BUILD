load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:stat_sink_config_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:stat_sink_missing_config_destroy",
        "//test/extensions/dynamic_modules/test_data/c:stat_sink_missing_config_new",
        "//test/extensions/dynamic_modules/test_data/c:stat_sink_missing_flush",
        "//test/extensions/dynamic_modules/test_data/c:stat_sink_missing_histogram_complete",
        "//test/extensions/dynamic_modules/test_data/c:stat_sink_no_op",
    ],
    deps = [
        "//envoy/registry",
        "//source/extensions/stat_sinks/dynamic_modules:config",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/stat_sinks/dynamic_modules/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "sink_test",
    srcs = ["sink_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:stat_sink_config_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:stat_sink_missing_config_new",
        "//test/extensions/dynamic_modules/test_data/c:stat_sink_no_op",
    ],
    deps = [
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/stat_sinks/dynamic_modules:sink_config_lib",
        "//source/extensions/stat_sinks/dynamic_modules:sink_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "abi_impl_test",
    srcs = ["abi_impl_test.cc"],
    deps = [
        "//source/extensions/dynamic_modules/abi",
        "//source/extensions/stat_sinks/dynamic_modules:sink_lib",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "integration_test",
    srcs = ["integration_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:stat_sink_integration_test",
    ],
    deps = [
        "//source/extensions/stat_sinks/dynamic_modules:config",
        "//test/extensions/dynamic_modules:util",
        "//test/integration:http_integration_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/stat_sinks/dynamic_modules/v3:pkg_cc_proto",
    ],
)
