load("@io_bazel_rules_go//proto:def.bzl", "go_proto_library")
load(
    "//bazel:external_proto_deps.bzl",
    "EXTERNAL_PROTO_GO_BAZEL_DEP_MAP",
)
load(
    ":proto_library.bzl",
    "api_cc_py_proto_library",
)

_GO_PROTO_SUFFIX = "_go_proto"
_GO_IMPORTPATH_PREFIX = "github.com/envoyproxy/go-control-plane/"
_IS_BZLMOD = str(Label("//:invalid")).startswith("@@")

def _proto_mapping(dep, proto_dep_map, proto_suffix):
    mapped = proto_dep_map.get(dep)
    if mapped == None:
        prefix = "@@" if _IS_BZLMOD else "@"
        prefix = prefix + Label(dep).repo_name if not dep.startswith("//") else ""
        return prefix + "//" + Label(dep).package + ":" + Label(dep).name + proto_suffix
    return mapped

def _go_proto_mapping(dep):
    return _proto_mapping(dep, EXTERNAL_PROTO_GO_BAZEL_DEP_MAP, _GO_PROTO_SUFFIX)

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
