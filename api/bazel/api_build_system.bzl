load("@com_envoyproxy_protoc_gen_validate//bazel:pgv_proto_library.bzl", "pgv_cc_proto_library")
load("@com_github_grpc_grpc//bazel:cc_grpc_library.bzl", "cc_grpc_library")
load("@com_github_grpc_grpc//bazel:python_rules.bzl", _py_proto_library = "py_proto_library")
load("@com_google_protobuf//bazel:proto_library.bzl", "proto_library")
load("@io_bazel_rules_go//go:def.bzl", "go_test")
load("@io_bazel_rules_go//proto:def.bzl", "go_proto_library")
load("@rules_cc//cc:defs.bzl", "cc_test")
load(
    "//bazel:external_proto_deps.bzl",
    "EXTERNAL_PROTO_CC_BAZEL_DEP_MAP",
    "EXTERNAL_PROTO_GO_BAZEL_DEP_MAP",
)
load(
    "//bazel/cc_proto_descriptor_library:builddefs.bzl",
    "cc_proto_descriptor_library",
)

EnvoyProtoDepsInfo = provider(fields = ["deps"])

_PY_PROTO_SUFFIX = "_py_proto"
_CC_PROTO_SUFFIX = "_cc_proto"
_CC_PROTO_DESCRIPTOR_SUFFIX = "_cc_proto_descriptor"
_CC_GRPC_SUFFIX = "_cc_grpc"
_GO_PROTO_SUFFIX = "_go_proto"
_GO_IMPORTPATH_PREFIX = "github.com/envoyproxy/go-control-plane/"
_JAVA_PROTO_SUFFIX = "_java_proto"

_COMMON_PROTO_DEPS = [
    "@com_google_protobuf//:any_proto",
    "@com_google_protobuf//:descriptor_proto",
    "@com_google_protobuf//:duration_proto",
    "@com_google_protobuf//:empty_proto",
    "@com_google_protobuf//:struct_proto",
    "@com_google_protobuf//:timestamp_proto",
    "@com_google_protobuf//:wrappers_proto",
    "@com_google_googleapis//google/api:http_proto",
    "@com_google_googleapis//google/api:httpbody_proto",
    "@com_google_googleapis//google/api:annotations_proto",
    "@com_google_googleapis//google/rpc:status_proto",
    "@com_envoyproxy_protoc_gen_validate//validate:validate_proto",
]

def _proto_mapping(dep, proto_dep_map, proto_suffix):
    mapped = proto_dep_map.get(dep)
    if mapped == None:
        prefix = "@" + Label(dep).workspace_name if not dep.startswith("//") else ""
        return prefix + "//" + Label(dep).package + ":" + Label(dep).name + proto_suffix
    return mapped

def _go_proto_mapping(dep):
    return _proto_mapping(dep, EXTERNAL_PROTO_GO_BAZEL_DEP_MAP, _GO_PROTO_SUFFIX)

def _cc_proto_mapping(dep):
    return _proto_mapping(dep, EXTERNAL_PROTO_CC_BAZEL_DEP_MAP, _CC_PROTO_SUFFIX)

def _api_cc_grpc_library(name, proto, deps = []):
    cc_grpc_library(
        name = name,
        srcs = [proto],
        deps = deps,
        proto_only = False,
        grpc_only = True,
        visibility = ["//visibility:public"],
    )

def api_cc_py_proto_library(
        name,
        visibility = ["//visibility:private"],
        srcs = [],
        deps = [],
        linkstatic = 0,
        has_services = 0,
        java = True):
    relative_name = ":" + name
    proto_library(
        name = name,
        srcs = srcs,
        deps = deps + _COMMON_PROTO_DEPS,
        visibility = visibility,
    )

    # This is to support Envoy Mobile using Protobuf-Lite.
    # Protobuf-Lite generated C++ code does not include reflection
    # capabilities but analogous functionality can be provided by
    # cc_proto_descriptor_library.
    cc_proto_descriptor_library(
        name = name + _CC_PROTO_DESCRIPTOR_SUFFIX,
        visibility = visibility,
        deps = [name],
    )

    cc_proto_library_name = name + _CC_PROTO_SUFFIX
    pgv_cc_proto_library(
        name = cc_proto_library_name,
        linkstatic = linkstatic,
        cc_deps = [_cc_proto_mapping(dep) for dep in deps] + [
            "@com_google_googleapis//google/api:http_cc_proto",
            "@com_google_googleapis//google/api:httpbody_cc_proto",
            "@com_google_googleapis//google/api:annotations_cc_proto",
            "@com_google_googleapis//google/rpc:status_cc_proto",
        ],
        deps = [relative_name],
        visibility = ["//visibility:public"],
    )

    # Uses gRPC implementation of py_proto_library.
    # https://github.com/grpc/grpc/blob/v1.59.1/bazel/python_rules.bzl#L160
    _py_proto_library(
        name = name + _PY_PROTO_SUFFIX,
        # Actual dependencies are resolved automatically from the proto_library dep tree.
        deps = [relative_name],
        visibility = ["//visibility:public"],
    )

    if java:
        native.java_proto_library(
            name = name + _JAVA_PROTO_SUFFIX,
            visibility = ["//visibility:public"],
            deps = [relative_name],
        )

    # Optionally define gRPC services
    if has_services:
        # TODO: when Python services are required, add to the below stub generations.
        cc_grpc_name = name + _CC_GRPC_SUFFIX
        cc_proto_deps = [cc_proto_library_name] + [_cc_proto_mapping(dep) for dep in deps]
        _api_cc_grpc_library(name = cc_grpc_name, proto = relative_name, deps = cc_proto_deps)

def api_cc_test(name, **kwargs):
    cc_test(
        name = name,
        **kwargs
    )

def api_go_test(name, **kwargs):
    go_test(
        name = name,
        **kwargs
    )

def api_proto_package(
        name = "pkg",
        srcs = [],
        deps = [],
        has_services = False,
        visibility = ["//visibility:public"]):
    if srcs == []:
        srcs = native.glob(["*.proto"])

    name = "pkg"
    api_cc_py_proto_library(
        name = name,
        visibility = visibility,
        srcs = srcs,
        deps = deps,
        has_services = has_services,
    )

    compilers = ["@io_bazel_rules_go//proto:go_proto", "@com_envoyproxy_protoc_gen_validate//bazel/go:pgv_plugin_go", "@envoy_api//bazel:vtprotobuf_plugin_go"]
    if has_services:
        compilers = ["@io_bazel_rules_go//proto:go_proto", "@io_bazel_rules_go//proto:go_grpc_v2", "@com_envoyproxy_protoc_gen_validate//bazel/go:pgv_plugin_go", "@envoy_api//bazel:vtprotobuf_plugin_go"]

    deps = (
        [_go_proto_mapping(dep) for dep in deps] +
        [
            "@com_envoyproxy_protoc_gen_validate//validate:go_default_library",
            "@org_golang_google_genproto_googleapis_api//annotations:annotations",
            "@org_golang_google_genproto_googleapis_rpc//status:status",
            "@org_golang_google_protobuf//types/known/anypb:go_default_library",
            "@org_golang_google_protobuf//types/known/durationpb:go_default_library",
            "@org_golang_google_protobuf//types/known/structpb:go_default_library",
            "@org_golang_google_protobuf//types/known/timestamppb:go_default_library",
            "@org_golang_google_protobuf//types/known/wrapperspb:go_default_library",
            "@com_github_planetscale_vtprotobuf//types/known/anypb",
            "@com_github_planetscale_vtprotobuf//types/known/durationpb",
            "@com_github_planetscale_vtprotobuf//types/known/emptypb",
            "@com_github_planetscale_vtprotobuf//types/known/fieldmaskpb",
            "@com_github_planetscale_vtprotobuf//types/known/structpb",
            "@com_github_planetscale_vtprotobuf//types/known/timestamppb",
            "@com_github_planetscale_vtprotobuf//types/known/wrapperspb",
        ]
    )
    go_proto_library(
        name = name + _GO_PROTO_SUFFIX,
        compilers = compilers,
        importpath = _GO_IMPORTPATH_PREFIX + native.package_name(),
        proto = name,
        visibility = ["//visibility:public"],
        deps = {dep: True for dep in deps}.keys(),
    )
