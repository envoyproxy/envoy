load("@pybind11_bazel//:build_defs.bzl", "pybind_extension")

licenses(["notice"])  # Apache 2

pybind_extension(
    name = "envoy_engine",
    srcs = [
        "module_definition.cc",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//library/cc:envoy_engine_cc_lib",
        # for log levels enum
        "@envoy//source/common/common:base_logger_lib",
    ],
)
