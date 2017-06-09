licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
    "envoy_proto_library",
)

envoy_package()

envoy_proto_library(
    name = "helloworld_proto",
    srcs = [":helloworld.proto"],
)

envoy_proto_library(
    name = "bookstore_proto",
    srcs = [":bookstore.proto"],
)

genrule(
    name = "bookstore_proto_descriptor",
    srcs = [
        "@googleapis//:http_api_protos_src",
        "@protobuf_bzl//:well_known_protos",
        ":bookstore.proto"
    ],
    outs = ["bookstore.descriptor"],
    cmd = """$(location //external:protoc) $(location :bookstore.proto) \
        -Iexternal/googleapis \
        -Iexternal/protobuf_bzl/src \
        -I$$(dirname $(location :bookstore.proto)) \
        --include_imports --descriptor_set_out=$@""",
    tools = ["//external:protoc"],
    visibility = ["//visibility:public"],
)

