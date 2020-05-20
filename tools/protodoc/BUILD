licenses(["notice"])  # Apache 2

load("@protodoc_pip3//:requirements.bzl", "requirement")

py_binary(
    name = "generate_empty",
    srcs = ["generate_empty.py"],
    visibility = ["//visibility:public"],
    deps = [":protodoc"],
)

py_binary(
    name = "protodoc",
    srcs = ["protodoc.py"],
    data = ["//docs:edge_defaults_manifest.yaml"],
    visibility = ["//visibility:public"],
    deps = [
        "//tools/api_proto_plugin",
        "//tools/config_validation:validate_fragment",
        "@com_envoyproxy_protoc_gen_validate//validate:validate_py",
        "@com_github_cncf_udpa//udpa/annotations:pkg_py_proto",
        "@com_google_protobuf//:protobuf_python",
        requirement("PyYAML"),
    ],
)
