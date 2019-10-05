licenses(["notice"])  # Apache 2

py_library(
    name = "api_proto_plugin",
    srcs = [
        "annotations.py",
        "plugin.py",
        "traverse.py",
        "type_context.py",
        "visitor.py",
    ],
    srcs_version = "PY3",
    visibility = ["//visibility:public"],
    deps = [
        "@com_google_protobuf//:protobuf_python",
    ],
)
