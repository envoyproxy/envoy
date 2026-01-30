load("@rules_cc//cc:defs.bzl", "cc_library")
load("@rules_python//python:defs.bzl", "py_binary")

licenses(["notice"])  # Apache 2

package(default_visibility = ["//visibility:public"])

exports_files([
    "version.go",
    "abi.h",
])

py_binary(
    name = "compute_abi_hash",
    srcs = ["compute_abi_hash.py"],
    visibility = ["//visibility:private"],
)

genrule(
    name = "abi_version_header",
    srcs = ["abi.h"],
    outs = ["abi_version.h"],
    cmd = """
        PYPATH=$$(realpath $$(dirname $(PYTHON3)))
        export PATH=$$PYPATH:$$PATH
        ./$(location :compute_abi_hash) $(location abi.h) $(location abi_version.h)
    """,
    toolchains = ["@rules_python//python:current_py_toolchain"],
    tools = [":compute_abi_hash"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "abi_version_lib",
    hdrs = [
        "abi_version.h",
    ],
    visibility = ["//visibility:public"],
    alwayslink = True,
)

cc_library(
    name = "abi",
    hdrs = ["abi.h"],
    visibility = ["//visibility:public"],
    deps = [":abi_version_lib"],
    alwayslink = True,
)
