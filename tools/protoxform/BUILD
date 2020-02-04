licenses(["notice"])  # Apache 2

py_binary(
    name = "protoxform",
    srcs = [
        "migrate.py",
        "options.py",
        "protoxform.py",
        "utils.py",
    ],
    data = ["//:.clang-format"],
    python_version = "PY3",
    visibility = ["//visibility:public"],
    deps = [
        "//tools/api_proto_plugin",
        "//tools/type_whisperer",
        "//tools/type_whisperer:api_type_db_proto_py_proto",
        "@com_envoyproxy_protoc_gen_validate//validate:validate_py",
        "@com_github_cncf_udpa//udpa/annotations:pkg_py_proto",
        "@com_google_googleapis//google/api:annotations_py_proto",
        "@com_google_protobuf//:protobuf_python",
        "@envoy_api_canonical//envoy/annotations:pkg_py_proto",
    ],
)
