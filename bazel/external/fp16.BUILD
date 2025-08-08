load("@rules_cc//cc:defs.bzl", "cc_library")

licenses(["notice"])  # MIT

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "FP16",
    hdrs = [
        "include/fp16.h",
        "include/fp16/bitcasts.h",
        "include/fp16/fp16.h",
    ],
    includes = ["include/"],
)
