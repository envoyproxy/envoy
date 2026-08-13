load(
    "//third_party/envoy/src/bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//third_party/envoy/src/test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])

envoy_package()

envoy_extension_cc_test(
    name = "opentelemetry_test",
    srcs = [
        "opentelemetry_tracer_impl_test.cc",
    ],
    copts = [
        # Make sure that headers included from opentelemetry-api use Abseil from Envoy
        # https://github.com/open-telemetry/opentelemetry-cpp/blob/v1.14.0/api/BUILD#L32
        "-DHAVE_ABSEIL",
    ],
    extension_names = [
        "envoy.tracers.opentelemetry",
        "envoy.tracers.opentelemetry.samplers.always_on",
    ],
    deps = [
        "//third_party/envoy/src/envoy/common:time_interface",
        "//third_party/envoy/src/envoy/runtime:runtime_interface",
        "//third_party/envoy/src/source/common/common:hex_lib",
        "//third_party/envoy/src/source/common/common:utility_lib",
        "//third_party/envoy/src/source/common/network:address_lib",
        "//third_party/envoy/src/source/common/network:utility_lib",
        "//third_party/envoy/src/source/common/protobuf:utility_lib",
        "//third_party/envoy/src/source/common/runtime:runtime_lib",
        "//third_party/envoy/src/source/extensions/tracers/opentelemetry:opentelemetry_tracer_lib",
        "//third_party/envoy/src/source/extensions/tracers/opentelemetry:trace_exporter",
        "//third_party/envoy/src/source/extensions/tracers/opentelemetry/samplers/always_on:config",
        "//third_party/envoy/src/test/mocks:common_lib",
        "//third_party/envoy/src/test/mocks/http:http_mocks",
        "//third_party/envoy/src/test/mocks/server:tracer_factory_context_mocks",
        "//third_party/envoy/src/test/mocks/stats:stats_mocks",
        "//third_party/envoy/src/test/mocks/stream_info:stream_info_mocks",
        "//third_party/envoy/src/test/mocks/tracing:tracing_mocks",
        "//third_party/envoy/src/test/mocks/upstream:cluster_manager_mocks",
        "//third_party/envoy/src/test/mocks/upstream:thread_local_cluster_mocks",
        "//third_party/envoy/src/test/test_common:simulated_time_system_lib",
        "//third_party/envoy/src/test/test_common:utility_lib",
        "//third_party/opentelemetry/cpp/api",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.tracers.opentelemetry"],
    deps = [
        "//third_party/envoy/src/api/envoy/config/trace/v3:pkg_cc_proto",
        "//third_party/envoy/src/source/extensions/tracers/opentelemetry:config",
        "//third_party/envoy/src/source/extensions/tracers/opentelemetry:trace_exporter",
        "//third_party/envoy/src/test/mocks/server:tracer_factory_context_mocks",
        "//third_party/envoy/src/test/mocks/server:tracer_factory_mocks",
        "//third_party/envoy/src/test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "span_context_extractor_test",
    srcs = ["span_context_extractor_test.cc"],
    extension_names = ["envoy.tracers.opentelemetry"],
    deps = [
        "//third_party/envoy/src/source/extensions/tracers/opentelemetry:opentelemetry_tracer_lib",
        "//third_party/envoy/src/test/test_common:status_utility_lib",
        "//third_party/envoy/src/test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "grpc_trace_exporter_test",
    srcs = ["grpc_trace_exporter_test.cc"],
    extension_names = ["envoy.tracers.opentelemetry"],
    deps = [
        "//third_party/envoy/src/source/extensions/tracers/opentelemetry:trace_exporter",
        "//third_party/envoy/src/test/mocks/grpc:grpc_mocks",
        "//third_party/envoy/src/test/mocks/http:http_mocks",
        "//third_party/envoy/src/test/test_common:logging_lib",
    ],
)

envoy_extension_cc_test(
    name = "http_trace_exporter_test",
    srcs = ["http_trace_exporter_test.cc"],
    extension_names = ["envoy.tracers.opentelemetry"],
    deps = [
        "//third_party/envoy/src/source/extensions/formatter/file_content:config",
        "//third_party/envoy/src/source/extensions/tracers/opentelemetry:trace_exporter",
        "//third_party/envoy/src/test/mocks/http:http_mocks",
        "//third_party/envoy/src/test/mocks/server:tracer_factory_context_mocks",
        "//third_party/envoy/src/test/mocks/stats:stats_mocks",
        "//third_party/envoy/src/test/mocks/upstream:cluster_manager_mocks",
        "//third_party/envoy/src/test/test_common:environment_lib",
        "//third_party/envoy/src/test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "operation_name_test",
    srcs = ["operation_name_test.cc"],
    extension_names = ["envoy.tracers.opentelemetry"],
    deps = [
        "//third_party/envoy/src/source/extensions/tracers/opentelemetry:config",
        "//third_party/envoy/src/source/extensions/tracers/opentelemetry:opentelemetry_tracer_lib",
        "//third_party/envoy/src/test/mocks/server:tracer_factory_context_mocks",
        "//third_party/envoy/src/test/mocks/stream_info:stream_info_mocks",
        "//third_party/envoy/src/test/mocks/tracing:tracing_mocks",
        "//third_party/envoy/src/test/mocks/upstream:cluster_manager_mocks",
        "//third_party/envoy/src/test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "grpc_trace_exporter_integration_test",
    srcs = ["grpc_trace_exporter_integration_test.cc"],
    extension_names = ["envoy.tracers.opentelemetry"],
    deps = [
        "//third_party/envoy/src/api/envoy/config/trace/v3:pkg_cc_proto",
        "//third_party/envoy/src/api/envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "//third_party/envoy/src/source/extensions/tracers/opentelemetry:config",
        "//third_party/envoy/src/source/extensions/tracers/opentelemetry:opentelemetry_tracer_lib",
        "//third_party/envoy/src/source/extensions/tracers/opentelemetry:trace_exporter",
        "//third_party/envoy/src/test/common/config:dummy_config_proto_cc_proto",
        "//third_party/envoy/src/test/integration:http_integration_lib",
        "//third_party/envoy/src/test/test_common:test_runtime_lib",
        "//third_party/envoy/src/test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "http_trace_exporter_integration_test",
    srcs = ["http_trace_exporter_integration_test.cc"],
    extension_names = ["envoy.tracers.opentelemetry"],
    deps = [
        "//third_party/envoy/src/api/envoy/config/trace/v3:pkg_cc_proto",
        "//third_party/envoy/src/api/envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "//third_party/envoy/src/source/extensions/tracers/opentelemetry:config",
        "//third_party/envoy/src/source/extensions/tracers/opentelemetry:opentelemetry_tracer_lib",
        "//third_party/envoy/src/source/extensions/tracers/opentelemetry:trace_exporter",
        "//third_party/envoy/src/test/integration:http_integration_lib",
        "//third_party/envoy/src/test/test_common:utility_lib",
    ],
)
