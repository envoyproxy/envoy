load("@rules_python//python:defs.bzl", "py_binary")
load("@rules_python//python:packaging.bzl", "py_wheel")
load("@pybind11_bazel//:build_defs.bzl", "pybind_extension", "pybind_library")
load("@python_abi//:abi.bzl", "python_abi")

licenses(["notice"])  # Apache 2

py_binary(
    name = "e2e_test",
    srcs = ["e2e_test.py"],
    data = [
        ":envoy_engine.so",
    ],
    main = "e2e_test.py",
    deps = [
        "//library/python/gevent_util",
    ],
)

pybind_library(
    name = "envoy_engine_lib",
    srcs = [
        "bytes_view.cc",
        "engine_builder_shim.cc",
        "stream_prototype_shim.cc",
        "stream_shim.cc",
    ],
    hdrs = [
        "bytes_view.h",
        "engine_builder_shim.h",
        "stream_prototype_shim.h",
        "stream_shim.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//library/cc:envoy_engine_cc_lib",
    ],
)

pybind_extension(
    name = "envoy_engine",
    srcs = [
        "module_definition.cc",
    ],
    visibility = ["//visibility:public"],
    deps = [
        ":envoy_engine_lib",
        "//library/cc:envoy_engine_cc_lib",
        # for log levels enum
        "@envoy//source/common/common:base_logger_lib",
    ],
)

py_wheel(
    name = "envoy_engine_whl",
    abi = python_abi(),
    distribution = "envoy_engine",
    platform = select({
        # TODO: build these on target platforms / architectures to verify
        # that this select statement is correct
        "//bazel:darwin_arm64": "macos_10_9_aarch64",
        "//bazel:darwin_x86_64": "macos_10_9_x86_64",
        "//bazel:darwin": "macos_10_9_x86_64",
        "//bazel:linux_aarch64": "manylinux2014_aarch64",
        "//bazel:linux_x86_64": "manylinux2014_x86_64",
    }),
    python_tag = python_abi(),
    strip_path_prefixes = ["library/python"],
    version = "0.0.1",
    deps = [
        ":envoy_engine.so",
    ],
)
