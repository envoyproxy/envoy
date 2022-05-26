load("@rules_python//python:defs.bzl", "py_binary")

licenses(["notice"])  # Apache 2

py_binary(
    name = "protoxform",
    srcs = [
        "options.py",
        "protoxform.py",
        "utils.py",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//tools/api_proto_plugin",
        "//tools/type_whisperer:api_type_db_proto_py_proto",
        "@com_envoyproxy_protoc_gen_validate//validate:validate_py",
        "@com_github_cncf_udpa//udpa/annotations:pkg_py_proto",
        "@com_github_cncf_udpa//xds/annotations/v3:pkg_py_proto",
        "@com_google_googleapis//google/api:annotations_py_proto",
        "@envoy_api//envoy/annotations:pkg_py_proto",
    ],
)

py_binary(
    name = "protoprint",
    srcs = [
        "options.py",
        "protoprint.py",
        "utils.py",
    ],
    data = [
        "//:.clang-format",
        "//:API_VERSION.txt",
        "//tools/type_whisperer:api_type_db.pb_text",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//tools/api_versioning:utils",
        "//tools/type_whisperer",
        "//tools/type_whisperer:api_type_db_proto_py_proto",
        "@com_envoyproxy_protoc_gen_validate//validate:validate_py",
        "@com_github_cncf_udpa//udpa/annotations:pkg_py_proto",
        "@com_github_cncf_udpa//xds/annotations/v3:pkg_py_proto",
        "@com_google_googleapis//google/api:annotations_py_proto",
        "@com_google_protobuf//:protobuf_python",
        "@envoy_api//envoy/annotations:pkg_py_proto",
    ],
)
