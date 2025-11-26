load("@rules_python//python:py_binary.bzl", "py_binary")
load("@rules_python//python:py_library.bzl", "py_library")
load("@rules_python//python:py_test.bzl", "py_test")

licenses(["notice"])  # Apache 2

py_library(
    name = "utils",
    srcs = ["utils.py"],
    visibility = ["//visibility:public"],
)

py_test(
    name = "utils_test",
    srcs = ["utils_test.py"],
    visibility = ["//visibility:public"],
    deps = [":utils"],
)

py_binary(
    name = "generate_api_version_header_bin",
    srcs = ["generate_api_version_header.py"],
    main = "generate_api_version_header.py",
    visibility = ["//visibility:public"],
    deps = [":utils"],
)

py_test(
    name = "generate_api_version_header_test",
    srcs = ["generate_api_version_header_test.py"],
    visibility = ["//visibility:public"],
    deps = [
        ":generate_api_version_header_bin",
        ":utils",
    ],
)
