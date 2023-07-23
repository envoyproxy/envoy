load("@rules_python//python:defs.bzl", "py_binary", "py_library", "py_test")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

py_library(
    name = "utils",
    srcs = ["utils.py"],
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
