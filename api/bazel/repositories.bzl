load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load(":envoy_http_archive.bzl", "envoy_http_archive")
load(":repository_locations.bzl", "REPOSITORY_LOCATIONS")

def api_dependencies():
    envoy_http_archive(
        "bazel_skylib",
        locations = REPOSITORY_LOCATIONS,
    )
    envoy_http_archive(
        "com_envoyproxy_protoc_gen_validate",
        locations = REPOSITORY_LOCATIONS,
    )
    envoy_http_archive(
        name = "com_google_googleapis",
        locations = REPOSITORY_LOCATIONS,
    )
    envoy_http_archive(
        name = "com_github_gogo_protobuf",
        locations = REPOSITORY_LOCATIONS,
        build_file_content = GOGOPROTO_BUILD_CONTENT,
    )
    envoy_http_archive(
        name = "prometheus_metrics_model",
        locations = REPOSITORY_LOCATIONS,
        build_file_content = PROMETHEUSMETRICS_BUILD_CONTENT,
    )
    envoy_http_archive(
        name = "opencensus_proto",
        locations = REPOSITORY_LOCATIONS,
    )
    envoy_http_archive(
        name = "kafka_source",
        locations = REPOSITORY_LOCATIONS,
        build_file_content = KAFKASOURCE_BUILD_CONTENT,
    )

GOGOPROTO_BUILD_CONTENT = """
load("@com_google_protobuf//:protobuf.bzl", "cc_proto_library", "py_proto_library")
load("@io_bazel_rules_go//proto:def.bzl", "go_proto_library")

proto_library(
    name = "gogo_proto",
    srcs = [
        "gogoproto/gogo.proto",
    ],
    deps = [
        "@com_google_protobuf//:descriptor_proto",
    ],
    visibility = ["//visibility:public"],
)

go_proto_library(
    name = "descriptor_go_proto",
    importpath = "github.com/golang/protobuf/protoc-gen-go/descriptor",
    proto = "@com_google_protobuf//:descriptor_proto",
    visibility = ["//visibility:public"],
)

cc_proto_library(
    name = "gogo_proto_cc",
    srcs = [
        "gogoproto/gogo.proto",
    ],
    default_runtime = "@com_google_protobuf//:protobuf",
    protoc = "@com_google_protobuf//:protoc",
    deps = ["@com_google_protobuf//:cc_wkt_protos"],
    visibility = ["//visibility:public"],
)

go_proto_library(
    name = "gogo_proto_go",
    importpath = "gogoproto",
    proto = ":gogo_proto",
    visibility = ["//visibility:public"],
    deps = [
        ":descriptor_go_proto",
    ],
)

py_proto_library(
    name = "gogo_proto_py",
    srcs = [
        "gogoproto/gogo.proto",
    ],
    default_runtime = "@com_google_protobuf//:protobuf_python",
    protoc = "@com_google_protobuf//:protoc",
    visibility = ["//visibility:public"],
    deps = ["@com_google_protobuf//:protobuf_python"],
)
"""

PROMETHEUSMETRICS_BUILD_CONTENT = """
load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library")
load("@io_bazel_rules_go//proto:def.bzl", "go_proto_library")

api_proto_library(
    name = "client_model",
    srcs = [
        "metrics.proto",
    ],
    visibility = ["//visibility:public"],
)

go_proto_library(
    name = "client_model_go_proto",
    importpath = "client_model",
    proto = ":client_model",
    visibility = ["//visibility:public"],
)
"""

OPENCENSUSTRACE_BUILD_CONTENT = """
load("@envoy_api//bazel:api_build_system.bzl", "api_proto_library")
load("@io_bazel_rules_go//proto:def.bzl", "go_proto_library")

api_proto_library(
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

KAFKASOURCE_BUILD_CONTENT = """

filegroup(
    name = "request_protocol_files",
    srcs = glob([
        "*Request.json",
    ]),
    visibility = ["//visibility:public"],
)

filegroup(
    name = "response_protocol_files",
    srcs = glob([
        "*Response.json",
    ]),
    visibility = ["//visibility:public"],
)

"""
