load("@rules_cc//cc:defs.bzl", "cc_library")

licenses(["notice"])  # Apache 2

package(default_visibility = ["//visibility:public"])

exports_files(["abi.h"])

cc_library(
    name = "abi",
    hdrs = ["abi.h"],
    visibility = ["//visibility:public"],
    alwayslink = True,
)
