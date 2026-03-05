load("@pybind11_bazel//:build_defs.bzl", "pybind_extension", "pybind_library")
load("@rules_python//python:packaging.bzl", "py_package", "py_wheel")
load("@python_abi//:abi.bzl", "abi_tag", "python_tag")

licenses(["notice"])  # Apache 2

package(default_visibility = ["//visibility:public"])

pybind_library(
    name = "envoy_engine_lib",
    srcs = [
        "engine_builder_shim.cc",
        "stream_prototype_shim.cc",
        "stream_shim.cc",
    ],
    hdrs = [
        "engine_builder_shim.h",
        "stream_prototype_shim.h",
        "stream_shim.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//library/cc:engine_builder_lib",
        "//library/cc:envoy_engine_cc_lib_no_stamp",
        "//library/common:engine_types_lib",
        "@envoy//source/common/buffer:buffer_lib",
        "@envoy//source/common/http:header_map_lib",
    ],
)

pybind_extension(
    name = "envoy_mobile/envoy_engine",
    srcs = ["module_definition.cc"],
    visibility = ["//visibility:public"],
    deps = [
        ":envoy_engine_lib",
        "//library/cc:engine_builder_lib",
        "//library/cc:envoy_engine_cc_lib_no_stamp",
        "//library/common:engine_types_lib",
        "//library/common/types:c_types_lib",
    ],
)

py_library(
    name = "envoy_mobile_lib",
    srcs = ["envoy_mobile/__init__.py"],
    data = [
        "envoy_mobile/py.typed",
        ":envoy_mobile/envoy_engine.so",
    ],
    visibility = ["//visibility:public"],
)

py_package(
    name = "envoy_mobile_package",
    packages = ["library.python.envoy_mobile"],
    deps = [":envoy_mobile_lib"],
)

# The version of the package. This can be customized by using --//library/python:version=x.y.z
# or by using --stamp and relying on the workspace status workspace_status_command.
py_wheel(
    name = "envoy_mobile_wheel",
    abi = abi_tag(),
    distribution = "envoy-mobile",
    platform = "manylinux2014_x86_64",
    python_tag = python_tag(),
    strip_path_prefixes = [
        "mobile/library/python",
        "library/python",
    ],
    version = "0.5.0",
    visibility = ["//visibility:public"],
    deps = [":envoy_mobile_package"],
)
