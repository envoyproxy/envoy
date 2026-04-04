load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@io_bazel_rules_go//proto:def.bzl", "go_proto_library")
load("@rules_cc//cc:defs.bzl", "cc_library")
load(":protobuf.bzl", "cc_proto_gen_validate", "java_proto_gen_validate")

_DEFAULT_GO_PROTOC = ["@io_bazel_rules_go//proto:go_proto"]

def pgv_go_proto_library(name, compilers = _DEFAULT_GO_PROTOC, proto = None, deps = [], **kwargs):
    go_proto_library(
        name = name,
        proto = proto,
        deps = ["@com_envoyproxy_protoc_gen_validate//validate:validate"] + deps,
        compilers = compilers + [
            "@com_envoyproxy_protoc_gen_validate//bazel/go:pgv_plugin_go",
        ],
        visibility = ["//visibility:public"],
        **kwargs
    )

def pgv_cc_proto_library(
        name,
        deps = [],
        cc_deps = [],
        copts = [],
        **kargs):
    """Bazel rule to create a C++ protobuf validation library from proto source files
    Args:
      name: the name of the pgv_cc_proto_library.
      deps: proto_library rules that contains the necessary .proto files.
      cc_deps: C++ dependencies of the protos being compiled. Likely cc_proto_library or pgv_cc_proto_library
      **kargs: other keyword arguments that are passed to cc_library.
    """

    cc_proto_library(
        name = name + "_cc_proto",
        deps = deps,
    )
    cc_proto_gen_validate(
        name = name + "_validate",
        deps = deps,
    )
    cc_library(
        name = name,
        hdrs = [":" + name + "_validate"],
        srcs = [":" + name + "_validate"],
        deps = cc_deps + [
            ":" + name + "_cc_proto",
            "@com_envoyproxy_protoc_gen_validate//validate:cc_validate",
            "@com_envoyproxy_protoc_gen_validate//validate:validate_cc",
            "@com_google_protobuf//:protobuf",
            "@re2",
        ],
        copts = copts + select({
            "@com_envoyproxy_protoc_gen_validate//bazel:windows_x86_64": ["-DWIN32"],
            "//conditions:default": [],
        }),
        alwayslink = 1,
        **kargs
    )

pgv_java_proto_library = java_proto_gen_validate
