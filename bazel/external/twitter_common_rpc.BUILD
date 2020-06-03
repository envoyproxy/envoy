load("@rules_python//python:defs.bzl", "py_library")

licenses(["notice"])  # Apache 2

py_library(
    name = "twitter_common_rpc",
    srcs = glob([
        "twitter/**/*.py",
    ]),
    visibility = ["//visibility:public"],
    deps = [
        "@com_github_twitter_common_finagle_thrift//:twitter_common_finagle_thrift",
        "@com_github_twitter_common_lang//:twitter_common_lang",
    ],
)
