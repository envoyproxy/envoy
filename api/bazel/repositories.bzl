GOOGLEAPIS_SHA = "d642131a6e6582fc226caf9893cb7fe7885b3411"  # May 23, 2018
GOGOPROTO_SHA = "1adfc126b41513cc696b209667c8656ea7aac67c"  # v1.0.0
GRPC_SHA = "befc7220cadb963755de86763a04ab6f9dc14200"  # v1.13.1
PROMETHEUS_SHA = "99fa1f4be8e564e8a6b613da7fa6f46c9edafc6c"  # Nov 17, 2017
OPENCENSUS_SHA = "ab82e5fdec8267dc2a726544b10af97675970847"  # May 23, 2018

PGV_GIT_SHA = "f9d2b11e44149635b23a002693b76512b01ae515"

load("@bazel_tools//tools/build_defs/repo:git.bzl", "git_repository")

def api_dependencies():
    git_repository(
        name = "com_lyft_protoc_gen_validate",
        remote = "https://github.com/lyft/protoc-gen-validate.git",
        commit = PGV_GIT_SHA,
    )
    native.new_http_archive(
        name = "googleapis",
        strip_prefix = "googleapis-" + GOOGLEAPIS_SHA,
        url = "https://github.com/googleapis/googleapis/archive/" + GOOGLEAPIS_SHA + ".tar.gz",
        # TODO(dio): Consider writing a Skylark macro for importing Google API proto.
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

    native.new_http_archive(
        name = "com_github_gogo_protobuf",
        strip_prefix = "protobuf-" + GOGOPROTO_SHA,
        url = "https://github.com/gogo/protobuf/archive/" + GOGOPROTO_SHA + ".tar.gz",
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

    native.new_http_archive(
        name = "prometheus_metrics_model",
        strip_prefix = "client_model-" + PROMETHEUS_SHA,
        url = "https://github.com/prometheus/client_model/archive/" + PROMETHEUS_SHA + ".tar.gz",
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

    native.new_http_archive(
        name = "io_opencensus_trace",
        strip_prefix = "opencensus-proto-" + OPENCENSUS_SHA + "/opencensus/proto/trace",
        url = "https://github.com/census-instrumentation/opencensus-proto/archive/" + OPENCENSUS_SHA + ".tar.gz",
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

    _com_github_grpc_grpc()

# TODO(bplotnick): port repository_locations.bzl repository management logic to here
def _com_github_grpc_grpc():
    git_repository(
        name = "com_github_grpc_grpc",
        remote = "https://github.com/grpc/grpc.git",
        commit = GRPC_SHA,
    )

    # Rebind some stuff to match what the gRPC Bazel is expecting.
    native.bind(
        name = "protobuf_headers",
        actual = "@com_google_protobuf//:protobuf_headers",
    )

    native.bind(
        name = "libssl",
        actual = "@boringssl//:ssl",
    )

    # Envoy requires this bind
    native.bind(
        name = "ssl",
        actual = "@boringssl//:ssl",
    )

    if "boringssl" not in native.existing_rules():
        native.http_archive(
            name = "boringssl",
            # chromium-68.0.3440.75
            url = "https://boringssl.googlesource.com/boringssl/+archive/372daf7042ffe3da1335743e7c93d78f1399aba7.tar.gz",
        )

    native.bind(
        name = "ares",
        actual = "@com_github_cares_cares//:ares",
    )

    native.bind(
        name = "cares",
        actual = "@com_github_cares_cares//:ares",
    )

    if "com_github_cares_cares" not in native.existing_rules():
        native.new_http_archive(
            name = "com_github_cares_cares",
            build_file = "@com_github_grpc_grpc//third_party:cares/cares.BUILD",
            strip_prefix = "c-ares-cares-1_14_0",
            url = "https://github.com/c-ares/c-ares/archive/cares-1_14_0.tar.gz",
        )

    native.bind(
        name = "grpc",
        actual = "@com_github_grpc_grpc//:grpc++",
    )

    native.bind(
        name = "grpc_health_proto",
        actual = "@envoy//bazel:grpc_health_proto",
    )

    # cc_proto_library via pgv_cc_proto_library requires these binds
    # Once https://github.com/bazelbuild/bazel/issues/1943 is fixed, we can just call grpc_deps()
    native.bind(
        name = "grpc_lib",
        actual = "@com_github_grpc_grpc//:grpc++",
    )

    native.bind(
        name = "grpc_cpp_plugin",
        actual = "@com_github_grpc_grpc//:grpc_cpp_plugin",
    )

    native.bind(
        name = "protobuf_clib",
        actual = "@com_google_protobuf//:protoc_lib",
    )
