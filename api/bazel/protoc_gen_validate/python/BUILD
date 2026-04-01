load("@pgv_pip_deps//:requirements.bzl", "all_requirements")
load("@rules_python//python:defs.bzl", "py_library")
load("@rules_python//python:pip.bzl", "compile_pip_requirements")

exports_files([
    "requirements.txt",
    "requirements.in",
    "setup.cfg",
])

compile_pip_requirements(
    name = "requirements",
    src = "requirements.in",
)

py_library(
    name = "validator_py",
    srcs = glob(["**/*.py"]),
    visibility = ["//visibility:public"],
    deps = all_requirements,
)
