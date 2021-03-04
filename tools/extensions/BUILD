load("@rules_python//python:defs.bzl", "py_binary")
load("//bazel:envoy_build_system.bzl", "envoy_package")
load("//source/extensions:all_extensions.bzl", "envoy_all_extensions")

licenses(["notice"])  # Apache 2

envoy_package()

py_binary(
    name = "generate_extension_db",
    srcs = ["generate_extension_db.py"],
    data = [
        "@com_github_bazelbuild_buildtools//buildozer:buildozer",
    ] + envoy_all_extensions(),
    python_version = "PY3",
    srcs_version = "PY3",
    visibility = ["//visibility:public"],
)
