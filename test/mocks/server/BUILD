licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

envoy_package()

envoy_cc_mock(
    name = "server_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//include/envoy/server:admin_interface",
        "//include/envoy/server:configuration_interface",
        "//include/envoy/server:drain_manager_interface",
        "//include/envoy/server:filter_config_interface",
        "//include/envoy/server:guarddog_interface",
        "//include/envoy/server:health_checker_config_interface",
        "//include/envoy/server:instance_interface",
        "//include/envoy/server:options_interface",
        "//include/envoy/server:worker_interface",
        "//include/envoy/ssl:context_manager_interface",
        "//source/common/singleton:manager_impl_lib",
        "//source/common/ssl:context_lib",
        "//source/common/stats:stats_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/api:api_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/init:init_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/router:router_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/tracing:tracing_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)
