load("@rules_python//python:defs.bzl", "py_binary")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "dynamic_modules_lib",
    srcs = ["dynamic_modules.cc"],
    hdrs = [
        "abi.h",
        "dynamic_modules.h",
    ],
    deps = [
        ":abi_version_lib",
        "//envoy/common:exception_lib",
    ],
)

envoy_cc_library(
    name = "abi_impl",
    srcs = ["abi_impl.cc"],
    deps = [
        ":dynamic_modules_lib",
        "//source/common/common:logger_lib",
    ],
)

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

envoy_cc_library(
    name = "abi_version_lib",
    hdrs = [
        "abi_version.h",
    ],
)
