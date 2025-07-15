load("@rules_cc//cc:cc_library.bzl", "cc_library")

licenses(["notice"])

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "fp16",
    hdrs = glob([
        "include/fp16.h",
        "include/fp16/*.h",
    ]),
    includes = ["include"],
    strip_include_prefix = "include",
)
