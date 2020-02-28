licenses(["notice"])  # Apache 2

py_binary(
    name = "generate_empty",
    srcs = ["generate_empty.py"],
    python_version = "PY3",
    visibility = ["//visibility:public"],
    deps = [":protodoc"],
)

py_binary(
    name = "protodoc",
    srcs = ["protodoc.py"],
    python_version = "PY3",
    visibility = ["//visibility:public"],
    deps = [
        "//tools/api_proto_plugin",
        "@com_envoyproxy_protoc_gen_validate//validate:validate_py",
        "@com_github_cncf_udpa//udpa/annotations:pkg_py_proto",
        "@com_google_protobuf//:protobuf_python",
    ],
)
