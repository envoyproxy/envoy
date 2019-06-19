licenses(["notice"])  # Apache 2

py_binary(
    name = "protodoc",
    srcs = ["protodoc.py"],
    visibility = ["//visibility:public"],
    deps = [
        "@com_envoyproxy_protoc_gen_validate//validate:validate_py",
        "@com_google_protobuf//:protobuf_python",
    ],
)
