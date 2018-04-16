licenses(["notice"])  # Apache 2

py_binary(
    name = "protodoc",
    srcs = ["protodoc.py"],
    visibility = ["//visibility:public"],
    deps = [
        "@com_google_protobuf//:protobuf_python",
        "@com_lyft_protoc_gen_validate//validate:validate_py",
    ],
)
