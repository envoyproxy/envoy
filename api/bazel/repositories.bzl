BAZEL_SKYLIB_RELEASE = "0.6.0"
BAZEL_SKYLIB_SHA256 = "eb5c57e4c12e68c0c20bc774bfbc60a568e800d025557bc4ea022c6479acc867"

GOGOPROTO_RELEASE = "1.2.0"
GOGOPROTO_SHA256 = "957c8f03cf595525d2a667035d9865a0930b3d446be0ab6eb76972934f925b00"

OPENCENSUS_RELEASE = "0.1.0"
OPENCENSUS_SHA256 = "4fd21cc6de63d7cb979fd749d8101ff425905aa0826fed26019d1c311fcf19a7"

PGV_RELEASE = "0.0.12"
PGV_SHA256 = "3be735345d1953d6d4c1cb89ace739cd6c98873d08b11218e181b0d3b0441627"

GOOGLEAPIS_GIT_SHA = "d642131a6e6582fc226caf9893cb7fe7885b3411"  # May 23, 2018
GOOGLEAPIS_SHA = "16f5b2e8bf1e747a32f9a62e211f8f33c94645492e9bbd72458061d9a9de1f63"

PROMETHEUS_GIT_SHA = "99fa1f4be8e564e8a6b613da7fa6f46c9edafc6c"  # Nov 17, 2017
PROMETHEUS_SHA = "783bdaf8ee0464b35ec0c8704871e1e72afa0005c3f3587f65d9d6694bf3911b"

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def api_dependencies():
    http_archive(
        name = "bazel_skylib",
        sha256 = BAZEL_SKYLIB_SHA256,
        strip_prefix = "bazel-skylib-" + BAZEL_SKYLIB_RELEASE,
        url = "https://github.com/bazelbuild/bazel-skylib/archive/" + BAZEL_SKYLIB_RELEASE + ".tar.gz",
    )
    http_archive(
        name = "com_lyft_protoc_gen_validate",
        sha256 = PGV_SHA256,
        strip_prefix = "protoc-gen-validate-" + PGV_RELEASE,
        url = "https://github.com/lyft/protoc-gen-validate/archive/v" + PGV_RELEASE + ".tar.gz",
    )
    http_archive(
        name = "googleapis",
        strip_prefix = "googleapis-" + GOOGLEAPIS_GIT_SHA,
        url = "https://github.com/googleapis/googleapis/archive/" + GOOGLEAPIS_GIT_SHA + ".tar.gz",
        # TODO(dio): Consider writing a Skylark macro for importing Google API proto.
        sha256 = GOOGLEAPIS_SHA,
        build_file_content = """
load("@com_google_protobuf//:protobuf.bzl", "cc_proto_library", "py_proto_library")
load("@io_bazel_rules_go//proto:def.bzl", "go_proto_library")

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
    srcs = [
        "google/api/httpbody.proto",
    ],
    default_runtime = "@com_google_protobuf//:protobuf",
    protoc = "@com_google_protobuf//:protoc",
    deps = ["@com_google_protobuf//:cc_wkt_protos"],
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
    srcs = [
        "google/api/annotations.proto",
        "google/api/http.proto",
    ],
    default_runtime = "@com_google_protobuf//:protobuf",
    protoc = "@com_google_protobuf//:protoc",
    deps = ["@com_google_protobuf//:cc_wkt_protos"],
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
     srcs = ["google/rpc/status.proto"],
     default_runtime = "@com_google_protobuf//:protobuf",
     protoc = "@com_google_protobuf//:protoc",
     deps = [
         "@com_google_protobuf//:cc_wkt_protos"
     ],
     visibility = ["//visibility:public"],
)

go_proto_library(
    name = "rpc_status_go_proto",
    importpath = "google.golang.org/genproto/googleapis/rpc/status",
    proto = ":rpc_status_protos_lib",
    visibility = ["//visibility:public"],
    deps = [
      "@com_github_golang_protobuf//ptypes/any:go_default_library",
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
""",
    )

    http_archive(
        name = "com_github_gogo_protobuf",
        sha256 = GOGOPROTO_SHA256,
        strip_prefix = "protobuf-" + GOGOPROTO_RELEASE,
        url = "https://github.com/gogo/protobuf/archive/v" + GOGOPROTO_RELEASE + ".tar.gz",
        build_file_content = """
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
        """,
    )

    http_archive(
        name = "prometheus_metrics_model",
        strip_prefix = "client_model-" + PROMETHEUS_GIT_SHA,
        url = "https://github.com/prometheus/client_model/archive/" + PROMETHEUS_GIT_SHA + ".tar.gz",
        sha256 = PROMETHEUS_SHA,
        build_file_content = """
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
        """,
    )

    http_archive(
        name = "io_opencensus_trace",
        sha256 = OPENCENSUS_SHA256,
        strip_prefix = "opencensus-proto-" + OPENCENSUS_RELEASE + "/src/opencensus/proto/trace/v1",
        url = "https://github.com/census-instrumentation/opencensus-proto/archive/v" + OPENCENSUS_RELEASE + ".tar.gz",
        build_file_content = """
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
        """,
    )
