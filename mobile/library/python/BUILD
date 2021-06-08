load("@pip//:requirements.bzl", "requirement")
load("@pybind11_bazel//:build_defs.bzl", "pybind_extension", "pybind_library")
load("@python_abi//:abi.bzl", "abi_tag", "python_tag")
load("@rules_python//python:defs.bzl", "py_library")
load("@rules_python//python:packaging.bzl", "py_wheel")

licenses(["notice"])  # Apache 2

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
    linkopts = select({
        "//bazel:darwin": ["-Wl,-exported_symbol,_PyInit_envoy_engine"],
        "//conditions:default": [],
    }),
    visibility = ["//visibility:public"],
    deps = [
        ":envoy_engine_lib",
        "//library/cc:envoy_engine_cc_lib",
        # for log levels enum
        "@envoy//source/common/common:base_logger_lib",
    ],
)

py_library(
    name = "envoy_requests",
    srcs = glob([
        "envoy_requests/**/*.py",
    ]),
    data = [
        ":envoy_engine.so",
    ],
    imports = ["."],
    visibility = ["//visibility:public"],
    deps = [
        requirement("gevent"),
    ],
)

py_wheel(
    name = "envoy_requests_whl",
    abi = abi_tag(),
    # TODO(crockeo): swap this out for better bus-factor contact info
    author = "Cerek Hillen",
    author_email = "chillen@lyft.com",
    description_file = ":README.rst",
    distribution = "envoy_requests",
    homepage = "https://github.com/envoyproxy/envoy-mobile",
    license = "Apache-2.0",
    platform = select({
        # TODO: build these on target platforms / architectures to verify
        # that this select statement is correct
        "//bazel:darwin_arm64": "macosx_10_9_aarch64",
        "//bazel:darwin_x86_64": "macosx_10_9_x86_64",
        "//bazel:darwin": "macosx_10_9_x86_64",
        "//bazel:linux_aarch64": "manylinux2014_aarch64",
        "//bazel:linux_x86_64": "manylinux2010_x86_64",
    }),
    python_tag = python_tag(),
    strip_path_prefixes = ["library/python"],
    version = "0.0.1a1",
    visibility = ["//visibility:public"],
    deps = [
        ":envoy_engine.so",
        ":envoy_requests",
    ],
)
