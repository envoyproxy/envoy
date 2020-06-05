load("@rules_python//python:defs.bzl", "py_binary", "py_test")

licenses(["notice"])  # Apache 2

py_binary(
    name = "merge_active_shadow",
    srcs = ["merge_active_shadow.py"],
    deps = [
        "//tools/api_proto_plugin",
        "@com_envoyproxy_protoc_gen_validate//validate:validate_py",
        "@com_github_cncf_udpa//udpa/annotations:pkg_py_proto",
        "@com_google_googleapis//google/api:annotations_py_proto",
        "@com_google_protobuf//:protobuf_python",
        "@envoy_api_canonical//envoy/annotations:pkg_py_proto",
    ],
)

py_test(
    name = "merge_active_shadow_test",
    srcs = ["merge_active_shadow_test.py"],
    deps = [
        ":merge_active_shadow",
        "//tools/api_proto_plugin",
        "@com_google_protobuf//:protobuf_python",
    ],
)

py_binary(
    name = "protoxform",
    srcs = [
        "migrate.py",
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
        "@com_google_googleapis//google/api:annotations_py_proto",
        "@envoy_api_canonical//envoy/annotations:pkg_py_proto",
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
        "//tools/type_whisperer:api_type_db.pb_text",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//tools/type_whisperer",
        "//tools/type_whisperer:api_type_db_proto_py_proto",
        "@com_envoyproxy_protoc_gen_validate//validate:validate_py",
        "@com_github_cncf_udpa//udpa/annotations:pkg_py_proto",
        "@com_google_googleapis//google/api:annotations_py_proto",
        "@com_google_protobuf//:protobuf_python",
        "@envoy_api_canonical//envoy/annotations:pkg_py_proto",
    ],
)
