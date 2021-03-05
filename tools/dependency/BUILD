load("@rules_python//python:defs.bzl", "py_binary")
load("//bazel:envoy_build_system.bzl", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

py_binary(
    name = "utils",
    srcs = ["utils.py"],
    data = [
        "//bazel:repository_locations.bzl",
        "@com_google_protobuf//:protobuf_python",
        "@com_google_protobuf//:well_known_protos",
        "@envoy_api_canonical//bazel:repository_locations.bzl",
        "@envoy_api_canonical//bazel:repository_locations_utils.bzl",
    ],
    python_version = "PY3",
    srcs_version = "PY3",
    visibility = ["//visibility:public"],
)

py_binary(
    name = "generate_external_dep_rst",
    srcs = ["generate_external_dep_rst.py"],
    data = [
        ":utils",
        "@com_google_protobuf//:protobuf_python",
        "@com_google_protobuf//:well_known_protos",
    ],
    visibility = ["//visibility:public"],
)
