load("@rules_python//python:defs.bzl", "py_test")
load("@headersplit_pip3//:requirements.bzl", "requirement")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

py_test(
    name = "headersplit_test",
    srcs = [
        "headersplit.py",
        "headersplit_test.py",
    ],
    data = glob(["code_corpus/**"]),
    main = "headersplit_test.py",
    python_version = "PY3",
    srcs_version = "PY3",
    tags = ["no-sandbox"],
    visibility = ["//visibility:public"],
    deps = [
        requirement("clang"),
    ],
)

py_test(
    name = "replace_includes_test",
    srcs = [
        "headersplit.py",
        "headersplit_test.py",
        "replace_includes.py",
        "replace_includes_test.py",
    ],
    data = glob(["code_corpus/**"]),
    main = "replace_includes_test.py",
    python_version = "PY3",
    srcs_version = "PY3",
    tags = ["no-sandbox"],
    visibility = ["//visibility:public"],
    deps = [
        requirement("clang"),
    ],
)
