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
    )
    external_http_archive(
        name = "com_google_googleapis",
    )
    external_http_archive(
        name = "com_github_cncf_udpa",
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
        name = "com_github_apache_skywalking_data_collect_protocol",
        build_file_content = SKYWALKING_DATA_COLLECT_PROTOCOL_BUILD_CONTENT,
    )

PROMETHEUSMETRICS_BUILD_CONTENT = """
load("@envoy_api//bazel:api_build_system.bzl", "api_cc_py_proto_library")
load("@io_bazel_rules_go//proto:def.bzl", "go_proto_library")

api_cc_py_proto_library(
    name = "client_model",
    srcs = [
        "metrics.proto",
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

SKYWALKING_DATA_COLLECT_PROTOCOL_BUILD_CONTENT = """
load("@rules_proto//proto:defs.bzl", "proto_library")
load("@rules_cc//cc:defs.bzl", "cc_proto_library")
load("@io_bazel_rules_go//proto:def.bzl", "go_proto_library")

proto_library(
    name = "protocol",
    srcs = [
        "common/Common.proto",
        "language-agent/Tracing.proto",
    ],
    visibility = ["//visibility:public"],
)

cc_proto_library(
    name = "protocol_cc_proto",
    deps = [":protocol"],
    visibility = ["//visibility:public"],
)

go_proto_library(
    name = "protocol_go_proto",
    proto = ":protocol",
    visibility = ["//visibility:public"],
)
"""
