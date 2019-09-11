load("@com_google_protobuf//:protobuf.bzl", _py_proto_library = "py_proto_library")
load("@com_envoyproxy_protoc_gen_validate//bazel:pgv_proto_library.bzl", "pgv_cc_proto_library")
load("@io_bazel_rules_go//proto:def.bzl", "go_grpc_library", "go_proto_library")
load("@io_bazel_rules_go//go:def.bzl", "go_test")
load("@com_github_grpc_grpc//bazel:cc_grpc_library.bzl", "cc_grpc_library")

_PY_SUFFIX = "_py"
_CC_SUFFIX = "_cc"
_CC_GRPC_SUFFIX = "_cc_grpc"
_CC_EXPORT_SUFFIX = "_export_cc"
_GO_PROTO_SUFFIX = "_go_proto"
_GO_IMPORTPATH_PREFIX = "github.com/envoyproxy/data-plane-api/api/"

_COMMON_PROTO_DEPS = [
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
    "@com_envoyproxy_protoc_gen_validate//validate:validate_proto",
]

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
def api_py_proto_library(name, srcs = [], deps = [], external_py_proto_deps = []):
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

def _api_cc_grpc_library(name, proto, deps = []):
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
        external_py_proto_deps = [],
        has_services = 0,
        linkstatic = None,
        require_py = 1):
    relative_name = ":" + name
    native.proto_library(
        name = name,
        srcs = srcs,
        deps = deps + external_proto_deps + _COMMON_PROTO_DEPS,
        visibility = visibility,
    )
    cc_proto_library_name = _Suffix(name, _CC_SUFFIX)
    pgv_cc_proto_library(
        name = cc_proto_library_name,
        linkstatic = linkstatic,
        cc_deps = [_LibrarySuffix(d, _CC_SUFFIX) for d in deps] + external_cc_proto_deps + [
            "@com_google_googleapis//google/api:http_cc_proto",
            "@com_google_googleapis//google/api:annotations_cc_proto",
            "@com_google_googleapis//google/rpc:status_cc_proto",
        ],
        deps = [relative_name],
        visibility = ["//visibility:public"],
    )
    py_export_suffixes = []
    if require_py:
        api_py_proto_library(name, srcs, deps, external_py_proto_deps)
        py_export_suffixes = ["_py", "_py_genproto"]

    # Optionally define gRPC services
    if has_services:
        # TODO: when Python services are required, add to the below stub generations.
        cc_grpc_name = _Suffix(name, _CC_GRPC_SUFFIX)
        cc_proto_deps = [cc_proto_library_name] + [_Suffix(_ToCanonicalLabel(x), _CC_SUFFIX) for x in deps]
        _api_cc_grpc_library(name = cc_grpc_name, proto = relative_name, deps = cc_proto_deps)

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
        deps = [_LibrarySuffix(d, _CC_EXPORT_SUFFIX) for d in proto_deps],
    )

def api_go_test(name, size, importpath, srcs = [], deps = []):
    go_test(
        name = name,
        size = size,
        srcs = srcs,
        importpath = importpath,
        deps = deps,
    )

_GO_BAZEL_RULE_MAPPING = {
    "@opencensus_proto//opencensus/proto/trace/v1:trace_proto": "@opencensus_proto//opencensus/proto/trace/v1:trace_proto_go",
    "@opencensus_proto//opencensus/proto/trace/v1:trace_config_proto": "@opencensus_proto//opencensus/proto/trace/v1:trace_and_config_proto_go",
    "@com_google_googleapis//google/api/expr/v1alpha1:syntax_proto": "@com_google_googleapis//google/api/expr/v1alpha1:cel_go_proto",
}

def go_proto_mapping(dep):
    mapped = _GO_BAZEL_RULE_MAPPING.get(dep)
    if mapped == None:
        return _Suffix("@" + Label(dep).workspace_name + "//" + Label(dep).package + ":" + Label(dep).name, _GO_PROTO_SUFFIX)
    return mapped

def api_proto_package(name = "pkg", srcs = [], deps = [], has_services = False, visibility = ["//visibility:public"]):
    if srcs == []:
        srcs = native.glob(["*.proto"])

    native.proto_library(
        name = name,
        srcs = srcs,
        deps = deps + _COMMON_PROTO_DEPS,
        visibility = visibility,
    )

    compilers = ["@io_bazel_rules_go//proto:go_proto", "//bazel:pgv_plugin_go"]
    if has_services:
        compilers = ["@io_bazel_rules_go//proto:go_grpc", "//bazel:pgv_plugin_go"]

    go_proto_library(
        name = _Suffix(name, _GO_PROTO_SUFFIX),
        compilers = compilers,
        importpath = _Suffix(_GO_IMPORTPATH_PREFIX, native.package_name()),
        proto = name,
        visibility = ["//visibility:public"],
        deps = [go_proto_mapping(dep) for dep in deps] + [
            "@com_github_golang_protobuf//ptypes:go_default_library",
            "@com_github_golang_protobuf//ptypes/any:go_default_library",
            "@com_github_golang_protobuf//ptypes/duration:go_default_library",
            "@com_github_golang_protobuf//ptypes/struct:go_default_library",
            "@com_github_golang_protobuf//ptypes/timestamp:go_default_library",
            "@com_github_golang_protobuf//ptypes/wrappers:go_default_library",
            "@com_envoyproxy_protoc_gen_validate//validate:go_default_library",
            "@com_google_googleapis//google/api:annotations_go_proto",
            "@com_google_googleapis//google/rpc:status_go_proto",
        ],
    )
