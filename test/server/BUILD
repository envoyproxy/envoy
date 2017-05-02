licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "backtrace_test",
    srcs = ["backtrace_test.cc"],
    tags = ["backtrace"],
    deps = ["//source/server:backtrace_lib"],
)

envoy_cc_test(
    name = "configuration_impl_test",
    srcs = ["configuration_impl_test.cc"],
    data = ["//test/common/ssl/test_data:certs"],
    deps = [
        "//source/common/event:dispatcher_lib",
        "//source/server:configuration_lib",
        "//test/mocks:common_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:environment_lib",
    ],
)

envoy_cc_test(
    name = "connection_handler_test",
    srcs = ["connection_handler_test.cc"],
    deps = [
        "//source/common/common:utility_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:address_lib",
        "//source/common/stats:stats_lib",
        "//source/server:connection_handler_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_cc_test(
    name = "drain_manager_impl_test",
    srcs = ["drain_manager_impl_test.cc"],
    deps = [
        "//source/server:drain_manager_lib",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_cc_test(
    name = "guarddog_impl_test",
    srcs = ["guarddog_impl_test.cc"],
    deps = [
        "//include/envoy/common:time_interface",
        "//source/common/common:utility_lib",
        "//source/common/stats:stats_lib",
        "//source/server:guarddog_lib",
        "//test/mocks:common_lib",
        "//test/mocks/server:server_mocks",
        "//test/mocks/stats:stats_mocks",
    ],
)

envoy_cc_test(
    name = "options_impl_test",
    srcs = ["options_impl_test.cc"],
    deps = [
        "//source/common/common:utility_lib",
        "//source/server:options_lib",
    ],
)

envoy_cc_test(
    name = "server_test",
    srcs = ["server_test.cc"],
    data = [
        "//test/config/integration:server.json",
        "//test/config/integration:server_config_files",
    ],
    deps = [
        "//source/server:server_lib",
        "//test/integration:integration_lib",
        "//test/mocks:common_lib",
        "//test/mocks/init:init_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/stats:stats_mocks",
    ],
)
