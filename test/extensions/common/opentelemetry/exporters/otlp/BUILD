load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "grpc_trace_exporter_test",
    srcs = ["grpc_trace_exporter_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/common/opentelemetry/exporters/otlp:trace_exporter_lib",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/http:http_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "http_trace_exporter_test",
    srcs = ["http_trace_exporter_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/version:version_lib",
        "//source/extensions/common/opentelemetry/exporters/otlp:trace_exporter_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:tracer_factory_context_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "populate_attribute_utils_test",
    srcs = ["populate_attribute_utils_test.cc"],
    copts = [
        # Make sure that headers included from opentelemetry-api use Abseil from Envoy
        # https://github.com/open-telemetry/opentelemetry-cpp/blob/v1.14.0/api/BUILD#L32
        "-DHAVE_ABSEIL",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/common/version:version_lib",
        "//source/extensions/common/opentelemetry/exporters/otlp:environment_lib",
        "//source/extensions/common/opentelemetry/exporters/otlp:populate_attribute_utils_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "trace_exporter_test",
    srcs = ["trace_exporter_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/common/opentelemetry/exporters/otlp:trace_exporter_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
    ],
)
