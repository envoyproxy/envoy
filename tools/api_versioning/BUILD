load("@rules_python//python:defs.bzl", "py_binary", "py_test")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

py_binary(
    name = "generate_api_version_header_bin",
    srcs = ["generate_api_version_header.py"],
    main = "generate_api_version_header.py",
    python_version = "PY3",
    srcs_version = "PY3",
    visibility = ["//visibility:public"],
)

py_test(
    name = "generate_api_version_header_test",
    srcs = ["generate_api_version_header_test.py"],
    python_version = "PY3",
    srcs_version = "PY3",
    visibility = ["//visibility:public"],
    deps = [
        ":generate_api_version_header_bin",
    ],
)
