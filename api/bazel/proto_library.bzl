"""Proto library creation rules for Envoy API."""

load("@com_envoyproxy_protoc_gen_validate//bazel:pgv_proto_library.bzl", "pgv_cc_proto_library")
load("@com_github_grpc_grpc//bazel:cc_grpc_library.bzl", "cc_grpc_library")
load("@com_github_grpc_grpc//bazel:python_rules.bzl", "py_proto_library")
load("@com_google_protobuf//bazel:java_proto_library.bzl", "java_proto_library")
load("@com_google_protobuf//bazel:proto_library.bzl", "proto_library")
load(
    "//bazel:external_proto_deps.bzl",
    "EXTERNAL_PROTO_CC_BAZEL_DEP_MAP",
)
load(
    "//bazel/cc_proto_descriptor_library:builddefs.bzl",
    "cc_proto_descriptor_library",
)

_PY_PROTO_SUFFIX = "_py_proto"
_CC_PROTO_SUFFIX = "_cc_proto"
_CC_PROTO_DESCRIPTOR_SUFFIX = "_cc_proto_descriptor"
_CC_GRPC_SUFFIX = "_cc_grpc"
_JAVA_PROTO_SUFFIX = "_java_proto"
_IS_BZLMOD = str(Label("//:invalid")).startswith("@@")

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
        prefix = "@@" if _IS_BZLMOD else "@"
        prefix = prefix + Label(dep).repo_name if not dep.startswith("//") else ""
        return prefix + "//" + Label(dep).package + ":" + Label(dep).name + proto_suffix
    return mapped

def _cc_proto_mapping(dep):
    return _proto_mapping(dep, EXTERNAL_PROTO_CC_BAZEL_DEP_MAP, _CC_PROTO_SUFFIX)

def _cc_proto_descriptor_library(name, visibility):
    """Helper to create cc_proto_descriptor_library with standard naming."""
    cc_proto_descriptor_library(
        name = name + _CC_PROTO_DESCRIPTOR_SUFFIX,
        visibility = visibility,
        deps = [name],
    )

def _cc_proto_library(name, visibility, linkstatic, deps, has_services):
    """Helper to create pgv_cc_proto_library and optionally cc_grpc_library with standard naming."""
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
        deps = [":" + name],
        visibility = ["//visibility:public"],
    )

    # Optionally define gRPC services
    if has_services:
        # TODO: when Python services are required, add to the below stub generations.
        cc_proto_deps = [cc_proto_library_name] + [_cc_proto_mapping(dep) for dep in deps]
        cc_grpc_library(
            name = name + _CC_GRPC_SUFFIX,
            srcs = [":" + name],
            deps = cc_proto_deps,
            proto_only = False,
            grpc_only = True,
            visibility = ["//visibility:public"],
        )

def _py_proto_library(name):
    """Helper to create py_proto_library with standard naming."""
    py_proto_library(
        name = name + _PY_PROTO_SUFFIX,
        # Actual dependencies are resolved automatically from the proto_library dep tree.
        deps = [":" + name],
        visibility = ["//visibility:public"],
    )

def _java_proto_library(name):
    """Helper to create java_proto_library with standard naming."""
    java_proto_library(
        name = name + _JAVA_PROTO_SUFFIX,
        visibility = ["//visibility:public"],
        deps = [":" + name],
    )

def api_cc_py_proto_library(
        name,
        visibility = ["//visibility:private"],
        srcs = [],
        deps = [],
        linkstatic = 0,
        has_services = 0,
        java = True):
    """Creates proto libraries for C++, Python, and Java.

    Args:
        name: Name of the proto_library target
        visibility: Visibility of the proto_library target
        srcs: Proto source files
        deps: Proto dependencies
        linkstatic: Whether to link statically
        has_services: Whether the proto has gRPC services
        java: Whether to generate Java proto library
    """
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
    _cc_proto_descriptor_library(name, visibility)

    _cc_proto_library(name, visibility, linkstatic, deps, has_services)

    # Uses gRPC implementation of py_proto_library.
    # https://github.com/grpc/grpc/blob/v1.59.1/bazel/python_rules.bzl#L160
    _py_proto_library(name)

    if java:
        _java_proto_library(name)
