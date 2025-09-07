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
    name = "grpc_trace_exporter_test",
    srcs = ["grpc_trace_exporter_test.cc"],
    extension_names = ["envoy.tracers.opentelemetry"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/common/opentelemetry/exporters/otlp:trace_exporter_lib",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/http:http_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "http_trace_exporter_test",
    srcs = ["http_trace_exporter_test.cc"],
    extension_names = ["envoy.tracers.opentelemetry"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/common/opentelemetry/exporters/otlp:trace_exporter_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:tracer_factory_context_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "grpc_trace_exporter_integration_test",
    srcs = ["grpc_trace_exporter_integration_test.cc"],
    extension_names = ["envoy.tracers.opentelemetry"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/common/opentelemetry/exporters/otlp:trace_exporter_lib",
        "//source/extensions/tracers/opentelemetry:config",
        "//source/extensions/tracers/opentelemetry:opentelemetry_tracer_lib",
        "//test/common/config:dummy_config_proto_cc_proto",
        "//test/integration:http_integration_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)
