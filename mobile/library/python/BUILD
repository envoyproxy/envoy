load("@pybind11_bazel//:build_defs.bzl", "pybind_extension", "pybind_library")
load("@rules_python//python:defs.bzl", "py_library")

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
    name = "envoy_engine",
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

# pure-python asyncio helpers which sit on top of the envoy_engine module.
# the glob ensures all of the submodules added above are included.
py_library(
    name = "async_client",
    srcs = glob(["async_client/**/*.py"]),
    data = [
        "//library/python:envoy_engine.so",
    ],
    visibility = ["//visibility:public"],
)
