load("@com_google_protobuf//:protobuf.bzl", "py_proto_library")
load("@com_envoyproxy_protoc_gen_validate//bazel:pgv_proto_library.bzl", "pgv_cc_proto_library")
load("@io_bazel_rules_go//proto:def.bzl", "go_grpc_library", "go_proto_library")
load("@io_bazel_rules_go//go:def.bzl", "go_test")
load("@com_github_grpc_grpc//bazel:cc_grpc_library.bzl", "cc_grpc_library")

_PY_SUFFIX = "_py"
_CC_SUFFIX = "_cc"
_CC_GRPC_SUFFIX = "_cc_grpc"
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

# TODO(htuch): Convert this to native py_proto_library once
# https://github.com/bazelbuild/bazel/issues/3935 and/or
# https://github.com/bazelbuild/bazel/issues/2626 are resolved.
def api_py_proto_library(name, srcs = [], deps = []):
    py_proto_library(
        name = _Suffix(name, _PY_SUFFIX),
        srcs = srcs,
        default_runtime = "@com_google_protobuf//:protobuf_python",
        protoc = "@com_google_protobuf//:protoc",
        deps = [_LibrarySuffix(d, _PY_SUFFIX) for d in deps] + [
            "@com_envoyproxy_protoc_gen_validate//validate:validate_py",
            "@googleapis//:api_httpbody_protos_py",
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
            "@io_bazel_rules_go//proto/wkt:any_go_proto",
            "@io_bazel_rules_go//proto/wkt:duration_go_proto",
            "@io_bazel_rules_go//proto/wkt:struct_go_proto",
            "@io_bazel_rules_go//proto/wkt:timestamp_go_proto",
            "@io_bazel_rules_go//proto/wkt:wrappers_go_proto",
            "@com_envoyproxy_protoc_gen_validate//validate:go_default_library",
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
            "@io_bazel_rules_go//proto/wkt:any_go_proto",
            "@io_bazel_rules_go//proto/wkt:duration_go_proto",
            "@io_bazel_rules_go//proto/wkt:struct_go_proto",
            "@io_bazel_rules_go//proto/wkt:wrappers_go_proto",
            "@com_envoyproxy_protoc_gen_validate//validate:go_default_library",
            "@googleapis//:http_api_go_proto",
        ],
    )

def api_cc_grpc_library(name, proto, deps = []):
    cc_grpc_library(
        name = name,
        srcs = [proto],
        deps = deps,
        proto_only = False,
        grpc_only = True,
        visibility = ["//visibility:public"],
    )

def _ToCanonicalLabel(label):
    # //my/app and //my/app:app are the same label. In places we mutate the incoming label adding different suffixes
    # in order to generate multiple targets in a single rule. //my/app:app_grpc_cc.
    # Skylark formatters and linters prefer the shorthand label whilst we need the latter.
    rel = Label("//" + native.package_name()).relative(label)
    return "//" + rel.package + ":" + rel.name

# This is api_proto_library plus some logic internal to //envoy/api.
def api_proto_library_internal(visibility = ["//visibility:private"], **kwargs):
    # //envoy/docs/build.sh needs visibility in order to generate documents.
    if visibility == ["//visibility:private"]:
        visibility = ["//docs"]
    elif visibility != ["//visibility:public"]:
        visibility = visibility + ["//docs"]

    api_proto_library(visibility = visibility, **kwargs)

def api_proto_library(
        name,
        visibility = ["//visibility:private"],
        srcs = [],
        deps = [],
        external_proto_deps = [],
        external_cc_proto_deps = [],
        has_services = 0,
        linkstatic = None,
        require_py = 1):
    this = ":" + name
    native.proto_library(
        name = name,
        srcs = srcs,
        deps = deps + external_proto_deps + [
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
            "@com_envoyproxy_protoc_gen_validate//validate:validate_proto",
        ],
        visibility = visibility,
    )
    cc_proto_library_name = _Suffix(name, _CC_SUFFIX)
    pgv_cc_proto_library(
        name = cc_proto_library_name,
        linkstatic = linkstatic,
        cc_deps = [_LibrarySuffix(d, _CC_SUFFIX) for d in deps] + external_cc_proto_deps + [
            "@com_github_gogo_protobuf//:gogo_proto_cc",
            "@googleapis//:http_api_protos",
            "@googleapis//:rpc_status_protos",
        ],
        deps = [this],
        visibility = ["//visibility:public"],
    )
    py_export_suffixes = []
    if require_py:
        api_py_proto_library(name, srcs, deps)
        py_export_suffixes = ["_py", "_py_genproto"]

    # Optionally define gRPC services
    if has_services:
        # TODO: replace uses of api_go_grpc_library and add functionality here.
        # TODO: when Python services are required, add to the below stub generations.
        cc_grpc_name = _Suffix(name, _CC_GRPC_SUFFIX)
        cc_proto_deps = [cc_proto_library_name] + [_Suffix(_ToCanonicalLabel(x), _CC_SUFFIX) for x in deps]
        api_cc_grpc_library(name = cc_grpc_name, proto = this, deps = cc_proto_deps)

    # Allow unlimited visibility for consumers
    export_suffixes = ["", "_cc", "_cc_validate"] + py_export_suffixes
    for s in export_suffixes:
        native.alias(
            name = name + "_export" + s,
            actual = name + s,
            visibility = ["//visibility:public"],
        )

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
