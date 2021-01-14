load("@rules_python//python:defs.bzl", "py_binary", "py_test")
load("@headersplit_pip3//:requirements.bzl", "requirement")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

py_binary(
    name = "headersplit",
    srcs = [
        "headersplit.py",
    ],
    python_version = "PY3",
    srcs_version = "PY3",
    visibility = ["//visibility:public"],
    deps = [
        requirement("clang"),
    ],
)

py_binary(
    name = "replace_includes",
    srcs = [
        "replace_includes.py",
    ],
    python_version = "PY3",
    srcs_version = "PY3",
    visibility = ["//visibility:public"],
    deps = [
        ":headersplit",
    ],
)

py_test(
    name = "headersplit_test",
    srcs = [
        "headersplit_test.py",
    ],
    data = glob(["code_corpus/**"]),
    python_version = "PY3",
    srcs_version = "PY3",
    tags = ["no-sandbox"],  # TODO (foreseeable): make this test run under sandbox
    visibility = ["//visibility:public"],
    deps = [
        requirement("clang"),
        ":headersplit",
    ],
)

py_test(
    name = "replace_includes_test",
    srcs = [
        "replace_includes_test.py",
    ],
    data = glob(["code_corpus/**"]),
    python_version = "PY3",
    srcs_version = "PY3",
    tags = ["no-sandbox"],  # TODO (foreseeable): make this test run under sandbox
    visibility = ["//visibility:public"],
    deps = [
        ":replace_includes",
    ],
)
