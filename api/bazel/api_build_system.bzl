load("@com_google_protobuf//:protobuf.bzl", _py_proto_library = "py_proto_library")
load("@com_envoyproxy_protoc_gen_validate//bazel:pgv_proto_library.bzl", "pgv_cc_proto_library")
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
def api_py_proto_library(name, srcs = [], deps = [], external_py_proto_deps = [], has_services = 0):
    _py_proto_library(
        name = _Suffix(name, _PY_SUFFIX),
        srcs = srcs,
        default_runtime = "@com_google_protobuf//:protobuf_python",
        protoc = "@com_google_protobuf//:protoc",
        deps = [_LibrarySuffix(d, _PY_SUFFIX) for d in deps] + external_py_proto_deps + [
            "@com_envoyproxy_protoc_gen_validate//validate:validate_py",
            "@com_google_googleapis//google/rpc:status_py_proto",
            "@com_google_googleapis//google/api:annotations_py_proto",
            "@com_google_googleapis//google/api:http_py_proto",
            "@com_google_googleapis//google/api:httpbody_py_proto",
            "@com_github_gogo_protobuf//:gogo_proto_py",
        ],
        visibility = ["//visibility:public"],
    )

# This defines googleapis py_proto_library. The repository does not provide its definition and requires
# overriding it in the consuming project (see https://github.com/grpc/grpc/issues/19255 for more details).
def py_proto_library(name, deps = []):
    srcs = [dep[:-6] + ".proto" if dep.endswith("_proto") else dep for dep in deps]
    proto_deps = []

    # py_proto_library in googleapis specifies *_proto rules in dependencies.
    # By rewriting *_proto to *.proto above, the dependencies in *_proto rules are not preserved.
    # As a workaround, manually specify the proto dependencies for the imported python rules.
    if name == "annotations_py_proto":
        proto_deps = proto_deps + [":http_py_proto"]
    _py_proto_library(
        name = name,
        srcs = srcs,
        default_runtime = "@com_google_protobuf//:protobuf_python",
        protoc = "@com_google_protobuf//:protoc",
        deps = proto_deps + ["@com_google_protobuf//:protobuf_python"],
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
            "@com_google_googleapis//google/rpc:status_go_proto",
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
            "@com_google_googleapis//google/api:annotations_go_proto",
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
def api_proto_library(
        name,
        visibility = ["//visibility:private"],
        srcs = [],
        deps = [],
        external_proto_deps = [],
        external_cc_proto_deps = [],
        external_py_proto_deps = [],
        has_services = 0,
        linkstatic = None,
        require_py = 1):
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
            "@com_google_googleapis//google/api:http_proto",
            "@com_google_googleapis//google/api:annotations_proto",
            "@com_google_googleapis//google/rpc:status_proto",
            "@com_github_gogo_protobuf//:gogo_proto",
            "@com_envoyproxy_protoc_gen_validate//validate:validate_proto",
        ],
        visibility = visibility,
    )
    pgv_cc_proto_library(
        name = _Suffix(name, _CC_SUFFIX),
        linkstatic = linkstatic,
        cc_deps = [_LibrarySuffix(d, _CC_SUFFIX) for d in deps] + external_cc_proto_deps + [
            "@com_github_gogo_protobuf//:gogo_proto_cc",
            "@com_google_googleapis//google/api:http_cc_proto",
            "@com_google_googleapis//google/api:annotations_cc_proto",
            "@com_google_googleapis//google/rpc:status_cc_proto",
        ],
        deps = [":" + name],
        visibility = ["//visibility:public"],
    )
    py_export_suffixes = []
    if (require_py == 1):
        api_py_proto_library(name, srcs, deps, external_py_proto_deps, has_services)
        py_export_suffixes = ["_py", "_py_genproto"]

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
