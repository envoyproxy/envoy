licenses(["notice"])  # Apache 2

py_binary(
    name = "protoxform",
    srcs = [
        "migrate.py",
        "options.py",
        "protoxform.py",
    ],
    python_version = "PY3",
    visibility = ["//visibility:public"],
    deps = [
        "//tools/api_proto_plugin",
        "@com_envoyproxy_protoc_gen_validate//validate:validate_py",
        "@com_google_googleapis//google/api:annotations_py_proto",
        "@com_google_protobuf//:protobuf_python",
    ],
)
