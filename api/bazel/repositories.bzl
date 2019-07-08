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
        name = "googleapis",
        locations = REPOSITORY_LOCATIONS,
        build_file_content = GOOGLEAPIS_BUILD_CONTENT,
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

GOOGLEAPIS_BUILD_CONTENT = """
load("@com_google_protobuf//:protobuf.bzl", "py_proto_library")
load("@io_bazel_rules_go//proto:def.bzl", "go_proto_library")
load("@com_github_grpc_grpc//bazel:cc_grpc_library.bzl", "cc_grpc_library")

filegroup(
    name = "api_httpbody_protos_src",
    srcs = [
        "google/api/httpbody.proto",
    ],
    visibility = ["//visibility:public"],
)

proto_library(
    name = "api_httpbody_protos_proto",
    srcs = [":api_httpbody_protos_src"],
    deps = ["@com_google_protobuf//:descriptor_proto"],
    visibility = ["//visibility:public"],
)

cc_proto_library(
    name = "api_httpbody_protos",
    deps = [":api_httpbody_protos_proto"],
    visibility = ["//visibility:public"],
)

py_proto_library(
    name = "api_httpbody_protos_py",
    srcs = [
        "google/api/httpbody.proto",
    ],
    include = ".",
    default_runtime = "@com_google_protobuf//:protobuf_python",
    protoc = "@com_google_protobuf//:protoc",
    visibility = ["//visibility:public"],
    deps = ["@com_google_protobuf//:protobuf_python"],
)

go_proto_library(
    name = "api_httpbody_go_proto",
    importpath = "google.golang.org/genproto/googleapis/api/httpbody",
    proto = ":api_httpbody_protos_proto",
    visibility = ["//visibility:public"],
    deps = [
      ":descriptor_go_proto",
    ],
)

filegroup(
    name = "http_api_protos_src",
    srcs = [
        "google/api/annotations.proto",
        "google/api/http.proto",
    ],
    visibility = ["//visibility:public"],
)

go_proto_library(
    name = "descriptor_go_proto",
    importpath = "github.com/golang/protobuf/protoc-gen-go/descriptor",
    proto = "@com_google_protobuf//:descriptor_proto",
    visibility = ["//visibility:public"],
)

proto_library(
    name = "http_api_protos_proto",
    srcs = [":http_api_protos_src"],
    deps = ["@com_google_protobuf//:descriptor_proto"],
    visibility = ["//visibility:public"],
)

cc_proto_library(
    name = "http_api_protos",
    deps = [":http_api_protos_proto"],
    visibility = ["//visibility:public"],
)

py_proto_library(
    name = "http_api_protos_py",
    srcs = [
        "google/api/annotations.proto",
        "google/api/http.proto",
    ],
    include = ".",
    default_runtime = "@com_google_protobuf//:protobuf_python",
    protoc = "@com_google_protobuf//:protoc",
    visibility = ["//visibility:public"],
    deps = ["@com_google_protobuf//:protobuf_python"],
)

go_proto_library(
    name = "http_api_go_proto",
    importpath = "google.golang.org/genproto/googleapis/api/annotations",
    proto = ":http_api_protos_proto",
    visibility = ["//visibility:public"],
    deps = [
      ":descriptor_go_proto",
    ],
)

filegroup(
     name = "rpc_status_protos_src",
     srcs = [
         "google/rpc/status.proto",
     ],
     visibility = ["//visibility:public"],
)

proto_library(
     name = "rpc_status_protos_lib",
     srcs = [":rpc_status_protos_src"],
     deps = ["@com_google_protobuf//:any_proto"],
     visibility = ["//visibility:public"],
)

cc_proto_library(
    name = "rpc_status_protos",
    deps = [":rpc_status_protos_lib"],
    visibility = ["//visibility:public"],
)

go_proto_library(
    name = "rpc_status_go_proto",
    importpath = "google.golang.org/genproto/googleapis/rpc/status",
    proto = ":rpc_status_protos_lib",
    visibility = ["//visibility:public"],
    deps = [
      "@io_bazel_rules_go//proto/wkt:any_go_proto",
    ],
)

py_proto_library(
     name = "rpc_status_protos_py",
     srcs = [
         "google/rpc/status.proto",
     ],
     include = ".",
     default_runtime = "@com_google_protobuf//:protobuf_python",
     protoc = "@com_google_protobuf//:protoc",
     visibility = ["//visibility:public"],
     deps = ["@com_google_protobuf//:protobuf_python"],
)

proto_library(
    name = "tracing_proto_proto",
    srcs = [
        "google/devtools/cloudtrace/v2/trace.proto",
        "google/devtools/cloudtrace/v2/tracing.proto",
    ],
    deps = [
        ":http_api_protos_proto",
        ":rpc_status_protos_lib",
        "@com_google_protobuf//:timestamp_proto",
        "@com_google_protobuf//:wrappers_proto",
        "@com_google_protobuf//:empty_proto",
    ],
)

cc_proto_library(
    name = "tracing_proto_cc",
    deps = [":tracing_proto_proto"],
)

cc_grpc_library(
    name = "tracing_proto",
    srcs = [":tracing_proto_proto"],
    deps = [":tracing_proto_cc"],
    grpc_only = True,
    visibility = ["@io_opencensus_cpp//opencensus:__subpackages__"],
)

"""

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
