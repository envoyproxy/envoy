licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "zipkin_test",
    srcs = [
        "span_buffer_test.cc",
        "tracer_test.cc",
        "util_test.cc",
        "zipkin_core_types_test.cc",
        "zipkin_tracer_impl_test.cc",
    ],
    external_deps = ["abseil_optional"],
    deps = [
        "//include/envoy/common:time_interface",
        "//include/envoy/runtime:runtime_interface",
        "//source/common/common:hex_lib",
        "//source/common/common:utility_lib",
        "//source/common/http:conn_manager_lib",
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//source/common/runtime:runtime_lib",
        "//source/extensions/tracers/zipkin:zipkin_lib",
        "//test/mocks:common_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/tracing:tracing_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    deps = [
        "//source/extensions/tracers/zipkin:config",
        "//test/mocks/server:server_mocks",
        "//test/test_common:utility_lib",
    ],
)
