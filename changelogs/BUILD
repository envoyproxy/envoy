load("@base_pip3//:requirements.bzl", "requirement")
load("@rules_python//python:defs.bzl", "py_binary", "py_library", "py_test")

licenses(["notice"])  # Apache 2

exports_files([
    "sections.yaml",
])

py_library(
    name = "lib",
    srcs = [
        "__init__.py",
        "_lib.py",
    ],
    visibility = ["//visibility:public"],
    deps = [requirement("pyyaml")],
)

py_library(
    name = "add_lib",
    srcs = [
        "__init__.py",
        "_lib.py",
        "add.py",
    ],
    visibility = ["//visibility:public"],
    deps = [":lib"],
)

py_library(
    name = "area_add_lib",
    srcs = [
        "__init__.py",
        "_lib.py",
        "area_add.py",
    ],
    visibility = ["//visibility:public"],
    deps = [":lib"],
)

py_binary(
    name = "add",
    srcs = [
        "__init__.py",
        "_lib.py",
        "add.py",
    ],
    data = glob(["areas.yaml", "sections.yaml"], allow_empty = True),
    main = "add.py",
    visibility = ["//visibility:public"],
    deps = [":lib"],
)

py_binary(
    name = "area-add",
    srcs = [
        "__init__.py",
        "_lib.py",
        "area_add.py",
    ],
    data = glob(["areas.yaml"], allow_empty = True),
    main = "area_add.py",
    visibility = ["//visibility:public"],
    deps = [":lib"],
)

py_test(
    name = "helpers_test",
    srcs = [
        "__init__.py",
        "_lib.py",
        "helpers_test.py",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":add_lib",
        ":area_add_lib",
        ":lib",
    ],
)

filegroup(
    name = "changelogs",
    srcs = glob(["*.*.*.yaml"]) + ["current.yaml"],
    visibility = ["//visibility:public"],
)

genrule(
    name = "summary",
    outs = ["summary.txt"],
    cmd = """
    cat $(location :summary.md) > $@
    """,
    tools = ["summary.md"],
    visibility = ["//visibility:public"],
)
