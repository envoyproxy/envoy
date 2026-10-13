load("@bazel_skylib//rules:common_settings.bzl", "string_flag")
load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load("@mobile_pip3//:requirements.bzl", "requirement")
load("@pybind11_bazel//:build_defs.bzl", "pybind_extension", "pybind_library")
load("@rules_python//python:defs.bzl", "py_library")
load("@rules_python//python:packaging.bzl", "py_package", "py_wheel")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

string_flag(
    name = "python_platform",
    build_setting_default = "any",
)

config_setting(
    name = "is_linux_x86_64",
    flag_values = {":python_platform": "manylinux2014_x86_64"},
)

config_setting(
    name = "is_macos_x86_64",
    flag_values = {":python_platform": "macosx_10_15_x86_64"},
)

config_setting(
    name = "is_macos_arm64",
    flag_values = {":python_platform": "macosx_11_0_arm64"},
)

string_flag(
    name = "python_version",
    build_setting_default = "3.12",
)

config_setting(
    name = "is_python_3_12",
    flag_values = {":python_version": "3.12"},
)

config_setting(
    name = "is_python_3_13",
    flag_values = {":python_version": "3.13"},
)

config_setting(
    name = "is_python_3_14",
    flag_values = {":python_version": "3.14"},
)

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

# This produces envoy_engine.so (or .pyd on Windows)
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

# We use a genrule to move the .so into the envoy_mobile/ package directory
# so that it is correctly bundled by py_wheel with the right import path.
genrule(
    name = "envoy_engine_so",
    srcs = [":envoy_engine.so"],
    outs = ["envoy_mobile/envoy_engine.so"],
    cmd = "cp $< $@",
)

# pure-python asyncio helpers which sit on top of the envoy_engine module.
# we place them in the envoy_mobile/ package directory for PyPI distribution.
py_library(
    name = "async_client",
    srcs = glob(["envoy_mobile/async_client/**/*.py"]),
    data = [
        ":envoy_engine_so",
    ],
    visibility = ["//visibility:public"],
)

py_library(
    name = "envoy_mobile_lib",
    srcs = glob(["envoy_mobile/**/*.py"]),
    data = [
        "envoy_mobile/py.typed",
        ":envoy_engine_so",
    ],
    imports = ["."],
    visibility = ["//visibility:public"],
    deps = [
        ":async_client",
        requirement("httpx"),
    ],
)

py_package(
    name = "envoy_mobile_package",
    deps = [":envoy_mobile_lib"],
)

# Example command for building an optimized wheel:
# bazel build -c opt --strip=always \
#   //library/python:envoy_mobile_wheel \
#   --//library/python:python_platform="manylinux2014_x86_64" \
py_wheel(
    name = "envoy_mobile_wheel",
    abi = select({
        ":is_python_3_14": "cp314",
        ":is_python_3_13": "cp313",
        "//conditions:default": "cp312",
    }),
    distribution = "envoy-mobile-client",
    platform = select({
        ":is_linux_x86_64": "manylinux2014_x86_64",
        ":is_macos_x86_64": "macosx_10_15_x86_64",
        ":is_macos_arm64": "macosx_11_0_arm64",
        "//conditions:default": "any",
    }),
    python_tag = select({
        ":is_python_3_14": "cp314",
        ":is_python_3_13": "cp313",
        "//conditions:default": "cp312",
    }),
    strip_path_prefixes = [
        "mobile/library/python",
        "library/python",
    ],
    version = "0.5.0",
    visibility = ["//visibility:public"],
    deps = [":envoy_mobile_package"],
)
