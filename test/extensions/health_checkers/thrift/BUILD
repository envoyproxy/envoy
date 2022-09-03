load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "thrift_test",
    srcs = ["thrift_test.cc"],
    extension_names = ["envoy.health_checkers.thrift"],
    deps = [
        "//source/common/api:api_lib",
        "//source/extensions/health_checkers/thrift",
        "//source/extensions/health_checkers/thrift:utility",
        "//test/common/upstream:utility_lib",
        "//test/extensions/filters/network/common/thrift:thrift_mocks",
        "//test/extensions/filters/network/common/thrift:test_utils_lib",
        "//test/extensions/filters/network/thrift_proxy:thrift_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:cluster_priority_set_mocks",
        "//test/mocks/upstream:health_check_event_logger_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/mocks/upstream:host_set_mocks",
        "//test/mocks/upstream:priority_set_mocks",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/network/thrift_proxy/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.health_checkers.thrift"],
    deps = [
        "//source/common/upstream:health_checker_lib",
        "//source/extensions/health_checkers/thrift:config",
        "//test/common/upstream:utility_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:health_checker_factory_context_mocks",
        "//test/mocks/upstream:health_checker_mocks",
        "//test/mocks/upstream:priority_set_mocks",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/extensions/filters/network/thrift_proxy/v3:pkg_cc_proto",
    ],
)
