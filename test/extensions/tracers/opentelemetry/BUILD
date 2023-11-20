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
    name = "opentelemetry_test",
    srcs = [
        "opentelemetry_tracer_impl_test.cc",
    ],
    extension_names = ["envoy.tracers.opentelemetry"],
    external_deps = ["abseil_optional"],
    deps = [
        "//envoy/common:time_interface",
        "//envoy/runtime:runtime_interface",
        "//source/common/common:hex_lib",
        "//source/common/common:utility_lib",
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/runtime:runtime_lib",
        "//source/extensions/tracers/opentelemetry:opentelemetry_tracer_lib",
        "//test/mocks:common_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:tracer_factory_context_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/tracing:tracing_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/mocks/upstream:thread_local_cluster_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.tracers.opentelemetry"],
    deps = [
        "//source/extensions/tracers/opentelemetry:config",
        "//test/mocks/server:tracer_factory_context_mocks",
        "//test/mocks/server:tracer_factory_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "span_context_extractor_test",
    srcs = ["span_context_extractor_test.cc"],
    extension_names = ["envoy.tracers.opentelemetry"],
    deps = [
        "//source/extensions/tracers/opentelemetry:opentelemetry_tracer_lib",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "grpc_trace_exporter_test",
    srcs = ["grpc_trace_exporter_test.cc"],
    extension_names = ["envoy.tracers.opentelemetry"],
    deps = [
        "//source/extensions/tracers/opentelemetry:trace_exporter",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/http:http_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "http_trace_exporter_test",
    srcs = ["http_trace_exporter_test.cc"],
    extension_names = ["envoy.tracers.opentelemetry"],
    deps = [
        "//source/extensions/tracers/opentelemetry:trace_exporter",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:tracer_factory_context_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:utility_lib",
    ],
)
