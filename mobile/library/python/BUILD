load("@pybind11_bazel//:build_defs.bzl", "pybind_extension", "pybind_library")

licenses(["notice"])  # Apache 2

pybind_library(
    name = "envoy_engine_lib",
    hdrs = [
        "bytes_view.h",
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
