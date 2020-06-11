load("@rules_python//python:defs.bzl", "py_library")

licenses(["notice"])  # Apache 2

py_library(
    name = "jinja2",
    srcs = glob(["jinja2/**/*.py"]),
    visibility = ["//visibility:public"],
    deps = ["@com_github_pallets_markupsafe//:markupsafe"],
)
