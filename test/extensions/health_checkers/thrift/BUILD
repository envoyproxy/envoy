load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_mock(
    name = "thrift_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//source/extensions/health_checkers/thrift:client_interface",
        "//test/mocks/network:connection_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "client_impl_test",
    srcs = ["client_impl_test.cc"],
    extension_names = ["envoy.health_checkers.thrift"],
    deps = [
        ":thrift_mocks",
        "//source/common/network:utility_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/health_checkers/thrift",
        "//source/extensions/health_checkers/thrift:client_lib",
        "//source/extensions/health_checkers/thrift:utility",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/test_common:simulated_time_system_lib",
    ],
)

envoy_extension_cc_test(
    name = "thrift_test",
    srcs = ["thrift_test.cc"],
    extension_names = ["envoy.health_checkers.thrift"],
    deps = [
        ":thrift_mocks",
        "//source/common/api:api_lib",
        "//source/extensions/health_checkers/thrift",
        "//source/extensions/health_checkers/thrift:utility",
        "//test/common/upstream:utility_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:cluster_priority_set_mocks",
        "//test/mocks/upstream:health_check_event_logger_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/mocks/upstream:host_set_mocks",
        "//test/mocks/upstream:priority_set_mocks",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
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
    ],
)
