load("@com_google_protobuf//:protobuf.bzl", "py_proto_library")
load("@com_lyft_protoc_gen_validate//bazel:pgv_proto_library.bzl", "pgv_cc_proto_library")
load("@io_bazel_rules_go//proto:def.bzl", "go_grpc_library", "go_proto_library")
load("@io_bazel_rules_go//go:def.bzl", "go_test")

_PY_SUFFIX = "_py"
_CC_SUFFIX = "_cc"
_GO_PROTO_SUFFIX = "_go_proto"
_GO_GRPC_SUFFIX = "_go_grpc"
_GO_IMPORTPATH_PREFIX = "github.com/envoyproxy/data-plane-api/api/"

def _Suffix(d, suffix):
    return d + suffix

def _LibrarySuffix(library_name, suffix):
    # Transform //a/b/c to //a/b/c:c in preparation for suffix operation below.
    if library_name.startswith("//") and ":" not in library_name:
        library_name += ":" + Label(library_name).name
    return _Suffix(library_name, suffix)

# TODO(htuch): has_services is currently ignored but will in future support
# gRPC stub generation.
# TODO(htuch): Convert this to native py_proto_library once
# https://github.com/bazelbuild/bazel/issues/3935 and/or
# https://github.com/bazelbuild/bazel/issues/2626 are resolved.
def api_py_proto_library(name, srcs = [], deps = [], has_services = 0):
    py_proto_library(
        name = _Suffix(name, _PY_SUFFIX),
        srcs = srcs,
        default_runtime = "@com_google_protobuf//:protobuf_python",
        protoc = "@com_google_protobuf//:protoc",
        deps = [_LibrarySuffix(d, _PY_SUFFIX) for d in deps] + [
            "@com_lyft_protoc_gen_validate//validate:validate_py",
            "@googleapis//:http_api_protos_py",
            "@googleapis//:rpc_status_protos_py",
            "@com_github_gogo_protobuf//:gogo_proto_py",
        ],
        visibility = ["//visibility:public"],
    )

def api_go_proto_library(name, proto, deps = []):
    go_proto_library(
        name = _Suffix(name, _GO_PROTO_SUFFIX),
        importpath = _Suffix(_GO_IMPORTPATH_PREFIX, name),
        proto = proto,
        visibility = ["//visibility:public"],
        deps = deps + [
            "@com_github_gogo_protobuf//:gogo_proto_go",
            "@com_github_golang_protobuf//ptypes/duration:go_default_library",
            "@com_github_golang_protobuf//ptypes/struct:go_default_library",
            "@com_github_golang_protobuf//ptypes/timestamp:go_default_library",
            "@com_github_golang_protobuf//ptypes/wrappers:go_default_library",
            "@com_github_golang_protobuf//ptypes/any:go_default_library",
            "@com_lyft_protoc_gen_validate//validate:go_default_library",
            "@googleapis//:rpc_status_go_proto",
        ],
    )

def api_go_grpc_library(name, proto, deps = []):
    go_grpc_library(
        name = _Suffix(name, _GO_GRPC_SUFFIX),
        importpath = _Suffix(_GO_IMPORTPATH_PREFIX, name),
        proto = proto,
        visibility = ["//visibility:public"],
        deps = deps + [
            "@com_github_gogo_protobuf//:gogo_proto_go",
            "@com_github_golang_protobuf//ptypes/duration:go_default_library",
            "@com_github_golang_protobuf//ptypes/struct:go_default_library",
            "@com_github_golang_protobuf//ptypes/wrappers:go_default_library",
            "@com_github_golang_protobuf//ptypes/any:go_default_library",
            "@com_lyft_protoc_gen_validate//validate:go_default_library",
            "@googleapis//:http_api_go_proto",
        ],
    )

# This is api_proto_library plus some logic internal to //envoy/api.
def api_proto_library_internal(visibility = ["//visibility:private"], **kwargs):
    # //envoy/docs/build.sh needs visibility in order to generate documents.
    if visibility == ["//visibility:private"]:
        visibility = ["//docs"]
    elif visibility != ["//visibility:public"]:
        visibility = visibility + ["//docs"]

    api_proto_library(visibility = visibility, **kwargs)

# TODO(htuch): has_services is currently ignored but will in future support
# gRPC stub generation.
# TODO(htuch): Automatically generate go_proto_library and go_grpc_library
# from api_proto_library.
def api_proto_library(name, visibility = ["//visibility:private"], srcs = [], deps = [], has_services = 0, require_py = 1):
    # This is now vestigial, since there are no direct consumers in
    # the data plane API. However, we want to maintain native proto_library support
    # in the proto graph to (1) support future C++ use of native rules with
    # cc_proto_library (or some Bazel aspect that works on proto_library) when
    # it can play well with the PGV plugin and (2) other language support that
    # can make use of native proto_library.

    native.proto_library(
        name = name,
        srcs = srcs,
        deps = deps + [
            "@com_google_protobuf//:any_proto",
            "@com_google_protobuf//:descriptor_proto",
            "@com_google_protobuf//:duration_proto",
            "@com_google_protobuf//:empty_proto",
            "@com_google_protobuf//:struct_proto",
            "@com_google_protobuf//:timestamp_proto",
            "@com_google_protobuf//:wrappers_proto",
            "@googleapis//:http_api_protos_proto",
            "@googleapis//:rpc_status_protos_lib",
            "@com_github_gogo_protobuf//:gogo_proto",
            "@com_lyft_protoc_gen_validate//validate:validate_proto",
        ],
        visibility = visibility,
    )

    # Under the hood, this is just an extension of the Protobuf library's
    # bespoke cc_proto_library. It doesn't consume proto_library as a proto
    # provider. Hopefully one day we can move to a model where this target and
    # the proto_library above are aligned.
    pgv_cc_proto_library(
        name = _Suffix(name, _CC_SUFFIX),
        srcs = srcs,
        deps = [_LibrarySuffix(d, _CC_SUFFIX) for d in deps],
        external_deps = [
            "@com_google_protobuf//:cc_wkt_protos",
            "@googleapis//:http_api_protos",
            "@googleapis//:rpc_status_protos",
            "@com_github_gogo_protobuf//:gogo_proto_cc",
        ],
        visibility = ["//visibility:public"],
    )
    if (require_py == 1):
        api_py_proto_library(name, srcs, deps, has_services)

def api_cc_test(name, srcs, proto_deps):
    native.cc_test(
        name = name,
        srcs = srcs,
        deps = [_LibrarySuffix(d, _CC_SUFFIX) for d in proto_deps],
    )

def api_go_test(name, size, importpath, srcs = [], deps = []):
    go_test(
        name = name,
        size = size,
        srcs = srcs,
        importpath = importpath,
        deps = deps,
    )
