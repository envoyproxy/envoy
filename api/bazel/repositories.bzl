load(":envoy_http_archive.bzl", "envoy_http_archive")
load(":external_deps.bzl", "load_repository_locations")
load(":repository_locations.bzl", "REPOSITORY_LOCATIONS_SPEC")

REPOSITORY_LOCATIONS = load_repository_locations(REPOSITORY_LOCATIONS_SPEC)

# Use this macro to reference any HTTP archive from bazel/repository_locations.bzl.
def external_http_archive(name, **kwargs):
    envoy_http_archive(
        name,
        locations = REPOSITORY_LOCATIONS,
        **kwargs
    )

def api_dependencies(bzlmod = False):
    # Dependencies needed for both WORKSPACE and bzlmod
    external_http_archive(
        name = "prometheus_metrics_model",
        build_file_content = PROMETHEUSMETRICS_BUILD_CONTENT,
    )
    external_http_archive(
        name = "com_github_chrusty_protoc_gen_jsonschema",
    )
    external_http_archive(
        name = "envoy_toolshed",
    )

    # WORKSPACE-only dependencies (available in BCR for bzlmod or not needed)
    if bzlmod:
        return

    external_http_archive(
        name = "bazel_skylib",
    )
    external_http_archive(
        name = "rules_jvm_external",
    )
    external_http_archive(
        name = "com_envoyproxy_protoc_gen_validate",
    )
    external_http_archive(
        name = "com_google_googleapis",
    )
    external_http_archive(
        name = "com_github_cncf_xds",
    )
    external_http_archive(
        name = "rules_buf",
    )
    external_http_archive(
        name = "rules_proto",
    )
    external_http_archive(
        name = "com_github_openzipkin_zipkinapi",
        build_file_content = ZIPKINAPI_BUILD_CONTENT,
    )
    external_http_archive(
        name = "opentelemetry_proto",
        build_file_content = OPENTELEMETRY_BUILD_CONTENT,
    )
    external_http_archive(
        name = "dev_cel",
    )

PROMETHEUSMETRICS_BUILD_CONTENT = """
load("@envoy_api//bazel:api_build_system.bzl", "api_cc_py_proto_library")
load("@io_bazel_rules_go//proto:def.bzl", "go_proto_library")

api_cc_py_proto_library(
    name = "client_model",
    srcs = [
        "io/prometheus/client/metrics.proto",
    ],
    visibility = ["//visibility:public"],
)

go_proto_library(
    name = "client_model_go_proto",
    importpath = "github.com/prometheus/client_model/go",
    proto = ":client_model",
    visibility = ["//visibility:public"],
)
"""

ZIPKINAPI_BUILD_CONTENT = """

load("@envoy_api//bazel:api_build_system.bzl", "api_cc_py_proto_library")
load("@io_bazel_rules_go//proto:def.bzl", "go_proto_library")

api_cc_py_proto_library(
    name = "zipkin",
    srcs = [
        "zipkin-jsonv2.proto",
        "zipkin.proto",
    ],
    visibility = ["//visibility:public"],
)

go_proto_library(
    name = "zipkin_go_proto",
    proto = ":zipkin",
    visibility = ["//visibility:public"],
)
"""

# Aligned target names with https://github.com/bazelbuild/bazel-central-registry/tree/main/modules/opentelemetry-proto
OPENTELEMETRY_BUILD_CONTENT = """
load("@com_github_grpc_grpc//bazel:cc_grpc_library.bzl", "cc_grpc_library")
load("@com_github_grpc_grpc//bazel:python_rules.bzl", "py_proto_library", "py_grpc_library")
load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@com_google_protobuf//bazel:proto_library.bzl", "proto_library")
load("@io_bazel_rules_go//proto:def.bzl", "go_proto_library", "go_grpc_library")

package(default_visibility = ["//visibility:public"])

proto_library(
    name = "common_proto",
    srcs = [
        "opentelemetry/proto/common/v1/common.proto",
    ],
)

cc_proto_library(
    name = "common_proto_cc",
    deps = [":common_proto"],
)

py_proto_library(
    name = "common_proto_py",
    deps = [":common_proto"],
)

go_proto_library(
    name = "common_proto_go",
    importpath = "go.opentelemetry.io/proto/otlp/common/v1",
    protos = [":common_proto"],
)

proto_library(
    name = "logs_proto",
    srcs = [
        "opentelemetry/proto/logs/v1/logs.proto",
    ],
    deps = [
        ":common_proto",
        ":resource_proto",
    ],
)

cc_proto_library(
    name = "logs_proto_cc",
    deps = [":logs_proto"],
)

py_proto_library(
    name = "logs_proto_py",
    deps = [":logs_proto"],
)

go_proto_library(
    name = "logs_proto_go",
    importpath = "go.opentelemetry.io/proto/otlp/logs/v1",
    protos = [":logs_proto"],
    deps = [
        ":common_proto_go",
        ":resource_proto_go",
    ],
)

proto_library(
    name = "logs_service_proto",
    srcs = [
        "opentelemetry/proto/collector/logs/v1/logs_service.proto",
    ],
    deps = [
        ":logs_proto",
    ],
)

cc_proto_library(
    name = "logs_service_proto_cc",
    deps = [":logs_service_proto"],
)

cc_grpc_library(
    name = "logs_service_grpc_cc",
    srcs = [":logs_service_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":logs_service_proto_cc"],
)

py_proto_library(
    name = "logs_service_proto_py",
    deps = [":logs_service_proto"],
)

py_grpc_library(
    name = "logs_service_grpc_py",
    srcs = [":logs_service_proto"],
    deps = [":logs_service_proto_py"],
)

go_grpc_library(
    name = "logs_service_grpc_go",
    protos = [":logs_service_proto"],
    importpath = "go.opentelemetry.io/proto/otlp/logs/v1",
    embed = [
        ":logs_proto_go",
    ],
)

proto_library(
    name = "metrics_proto",
    srcs = [
        "opentelemetry/proto/metrics/v1/metrics.proto",
    ],
    deps = [
        ":common_proto",
        ":resource_proto",
    ],
)

cc_proto_library(
    name = "metrics_proto_cc",
    deps = [":metrics_proto"],
)

py_proto_library(
    name = "metrics_proto_py",
    deps = [":metrics_proto"],
)

go_proto_library(
    name = "metrics_proto_go",
    importpath = "go.opentelemetry.io/proto/otlp/metrics/v1",
    protos = [":metrics_proto"],
    deps = [
        ":common_proto_go",
        ":resource_proto_go",
    ],
)

proto_library(
    name = "metrics_service_proto",
    srcs = [
        "opentelemetry/proto/collector/metrics/v1/metrics_service.proto",
    ],
    deps = [
        ":metrics_proto",
    ],
)

cc_proto_library(
    name = "metrics_service_proto_cc",
    deps = [":metrics_service_proto"],
)

cc_grpc_library(
    name = "metrics_service_grpc_cc",
    srcs = [":metrics_service_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":metrics_service_proto_cc"],
)

py_proto_library(
    name = "metrics_service_proto_py",
    deps = [":metrics_service_proto"],
)

py_grpc_library(
    name = "metrics_service_grpc_py",
    srcs = [":metrics_service_proto"],
    deps = [":metrics_service_proto_py"],
)

go_grpc_library(
    name = "metrics_service_grpc_go",
    protos = [":metrics_service_proto"],
    importpath = "go.opentelemetry.io/proto/otlp/metrics/v1",
    embed = [
        ":metrics_proto_go",
    ],
)

proto_library(
    name = "resource_proto",
    srcs = [
        "opentelemetry/proto/resource/v1/resource.proto",
    ],
    deps = [
        ":common_proto",
    ],
)

cc_proto_library(
    name = "resource_proto_cc",
    deps = [":resource_proto"],
)

py_proto_library(
    name = "resource_proto_py",
    deps = [":resource_proto"],
)

go_proto_library(
    name = "resource_proto_go",
    importpath = "go.opentelemetry.io/proto/otlp/resource/v1",
    protos = [":resource_proto"],
    deps = [
        ":common_proto_go",
    ],
)

proto_library(
    name = "trace_proto",
    srcs = [
        "opentelemetry/proto/trace/v1/trace.proto",
    ],
    deps = [
        ":common_proto",
        ":resource_proto",
    ],
)

cc_proto_library(
    name = "trace_proto_cc",
    deps = [":trace_proto"],
)

py_proto_library(
    name = "trace_proto_py",
    deps = [":trace_proto"],
)

go_proto_library(
    name = "trace_proto_go",
    importpath = "go.opentelemetry.io/proto/otlp/trace/v1",
    protos = [":trace_proto"],
    deps = [
        ":common_proto_go",
        ":resource_proto_go",
    ],
)

proto_library(
    name = "trace_service_proto",
    srcs = [
        "opentelemetry/proto/collector/trace/v1/trace_service.proto",
    ],
    deps = [
        ":trace_proto",
    ],
)

cc_proto_library(
    name = "trace_service_proto_cc",
    deps = [":trace_service_proto"],
)

cc_grpc_library(
    name = "trace_service_grpc_cc",
    srcs = [":trace_service_proto"],
    generate_mocks = True,
    grpc_only = True,
    deps = [":trace_service_proto_cc"],
)

py_proto_library(
    name = "trace_service_proto_py",
    deps = [":trace_service_proto"],
)

py_grpc_library(
    name = "trace_service_grpc_py",
    srcs = [":trace_service_proto"],
    deps = [":trace_service_proto_py"],
)

go_grpc_library(
    name = "trace_service_grpc_go",
    protos = [":trace_service_proto"],
    importpath = "go.opentelemetry.io/proto/otlp/trace/v1",
    embed = [
        ":trace_proto_go",
    ],
)
"""
