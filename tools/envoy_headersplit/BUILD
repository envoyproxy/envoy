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
        "headersplit_test.py",
        "headersplit.py"
    ],
    data = glob(["code_corpus/**"]),
    deps = [
        requirement("clang"),
    ],
    main = "headersplit_test.py",
    python_version = "PY3",
    srcs_version = "PY3",
    visibility = ["//visibility:public"],
)

