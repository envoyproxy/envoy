load("@rules_cc//cc:cc_library.bzl", "cc_library")

licenses(["notice"])

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "simdutf",
    srcs = ["src/simdutf.cpp"],
    hdrs = glob([
        "include/simdutf.h",
        "include/simdutf/*.h",
        "include/simdutf/**/*.h",
        "src/**/*.h",
        "src/**/*.cpp",
    ]),
    copts = [
        "-std=c++17",
        "-DSIMDUTF_IMPLEMENTATION",
    ],
    includes = [
        "include",
        "src",
    ],
)
