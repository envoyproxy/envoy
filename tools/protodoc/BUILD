licenses(["notice"])  # Apache 2

py_binary(
    name = "protodoc",
    srcs = ["protodoc.py"],
    python_version = "PY3",
    visibility = ["//visibility:public"],
    deps = [
        "//tools/api_proto_plugin",
        "@com_envoyproxy_protoc_gen_validate//validate:validate_py",
        "@com_google_protobuf//:protobuf_python",
    ],
)
