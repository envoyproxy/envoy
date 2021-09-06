load("@dev_pip3//:requirements.bzl", dev_requirement = "requirement")
load("@rules_python//python:defs.bzl", "py_library")

py_library(
    name = "proto_rst_srcs",
    srcs = ["proto_rst_srcs.py"],
    visibility = ["//visibility:public"],
)

py_library(
    name = "empty_extensions",
    srcs = ["empty_extensions.py"],
    visibility = ["//visibility:public"],
)

py_library(
    name = "interpolate_repository_locations",
    srcs = ["interpolate_repository_locations.py"],
    visibility = ["//visibility:public"],
)

py_library(
    name = "add_api_repository_locations",
    srcs = ["add_api_repository_locations.py"],
    deps = ["//api/bazel:repository_locations_py"],
    visibility = ["//visibility:public"],
)

py_library(
    name = "build_categories",
    srcs = ["build_categories.py"],
    visibility = ["//visibility:public"],
)

py_library(
    name = "add_contrib_extensions_metadata",
    srcs = ["add_contrib_extensions_metadata.py"],
    deps = ["//contrib:extensions_metadata_py"],
    visibility = ["//visibility:public"],
)

py_library(
    name = "extension_security_postures",
    srcs = ["extension_security_postures.py"],
    visibility = ["//visibility:public"],
    deps = [dev_requirement("envoy.docs.abstract")],
)

py_library(
    name = "protodoc_protobuf_manifest",
    srcs = ["protodoc_protobuf_manifest.py"],
    visibility = ["//visibility:public"],
    deps = [
        "@envoy//tools/protodoc:manifest_proto_py_proto",
        "@com_google_protobuf//:protobuf_python",
    ],
)
