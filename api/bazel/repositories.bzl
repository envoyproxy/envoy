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

def api_dependencies():
    external_http_archive(
        name = "bazel_skylib",
    )
    external_http_archive(
        name = "com_envoyproxy_protoc_gen_validate",
        patch_args = ["-p1"],
        patches = ["@envoy//bazel:pgv.patch"],
    )
    external_http_archive(
        name = "com_google_googleapis",
    )

    external_http_archive(
        name = "com_github_cncf_xds",
    )

    # Needed until @com_github_grpc_grpc renames @com_github_cncf_udpa
    # to @com_github_cncf_xds as well.
    external_http_archive(
        name = "com_github_cncf_udpa",
        location_name = "com_github_cncf_xds",
    )

    external_http_archive(
        name = "prometheus_metrics_model",
        build_file_content = PROMETHEUSMETRICS_BUILD_CONTENT,
    )
    external_http_archive(
        name = "opencensus_proto",
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
        name = "com_github_bufbuild_buf",
        build_file_content = BUF_BUILD_CONTENT,
    )

    external_http_archive(
        name = "com_github_chrusty_protoc_gen_jsonschema",
    )
    external_http_archive(
        name = "rules_proto_grpc",
    )

    external_http_archive(
        name = "envoy_toolshed",
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

OPENCENSUSTRACE_BUILD_CONTENT = """
load("@envoy_api//bazel:api_build_system.bzl", "api_cc_py_proto_library")
load("@io_bazel_rules_go//proto:def.bzl", "go_proto_library")

api_cc_py_proto_library(
    name = "trace_model",
    srcs = [
        "trace.proto",
    ],
    visibility = ["//visibility:public"],
)

go_proto_library(
    name = "trace_model_go_proto",
    importpath = "trace_model",
    proto = ":trace_model",
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

OPENTELEMETRY_BUILD_CONTENT = """
load("@envoy_api//bazel:api_build_system.bzl", "api_cc_py_proto_library")
load("@io_bazel_rules_go//proto:def.bzl", "go_proto_library")

api_cc_py_proto_library(
    name = "common",
    srcs = [
        "opentelemetry/proto/common/v1/common.proto",
    ],
    visibility = ["//visibility:public"],
)

api_cc_py_proto_library(
    name = "resource",
    srcs = [
        "opentelemetry/proto/resource/v1/resource.proto",
    ],
    deps = [
        "//:common",
    ],
    visibility = ["//visibility:public"],
)

go_proto_library(
    name = "common_go_proto",
    importpath = "go.opentelemetry.io/proto/otlp/common/v1",
    proto = ":common",
    visibility = ["//visibility:public"],
)

# TODO(snowp): Generating one Go package from all of these protos could cause problems in the future,
# but nothing references symbols from collector or resource so we're fine for now.
api_cc_py_proto_library(
    name = "logs",
    srcs = [
        "opentelemetry/proto/collector/logs/v1/logs_service.proto",
        "opentelemetry/proto/logs/v1/logs.proto",
    ],
    deps = [
        "//:common",
        "//:resource",
    ],
    visibility = ["//visibility:public"],
)

go_proto_library(
    name = "logs_go_proto",
    importpath = "go.opentelemetry.io/proto/otlp/logs/v1",
    proto = ":logs",
    visibility = ["//visibility:public"],
)

api_cc_py_proto_library(
    name = "metrics",
    srcs = [
        "opentelemetry/proto/collector/metrics/v1/metrics_service.proto",
        "opentelemetry/proto/metrics/v1/metrics.proto",
    ],
    deps = [
        "//:common",
        "//:resource",
    ],
    visibility = ["//visibility:public"],
)

go_proto_library(
    name = "metrics_go_proto",
    importpath = "go.opentelemetry.io/proto/otlp/metrics/v1",
    proto = ":metrics",
    visibility = ["//visibility:public"],
)

api_cc_py_proto_library(
    name = "trace",
    srcs = [
        "opentelemetry/proto/collector/trace/v1/trace_service.proto",
        "opentelemetry/proto/trace/v1/trace.proto",
    ],
    deps = [
        "//:common",
        "//:resource",
    ],
    visibility = ["//visibility:public"],
)
"""

BUF_BUILD_CONTENT = """
package(
    default_visibility = ["//visibility:public"],
)

filegroup(
    name = "buf",
    srcs = [
        "@com_github_bufbuild_buf//:bin/buf",
    ],
    tags = ["manual"], # buf is downloaded as a linux binary; tagged manual to prevent build for non-linux users
)
"""
