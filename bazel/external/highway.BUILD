load("@rules_cc//cc:cc_library.bzl", "cc_library")

licenses(["notice"])

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "hwy",
    srcs = glob(
        [
            "hwy/*.cc",
            "hwy/contrib/algo/*.cc",
            "hwy/contrib/image/*.cc",
            "hwy/contrib/math/*.cc",
            "hwy/contrib/random/*.cc",
            "hwy/contrib/sort/*.cc",
            "hwy/contrib/unroller/*.cc",
        ],
        exclude = [
            "hwy/*test*.cc",
            "hwy/contrib/**/*test*.cc",
        ],
    ),
    hdrs = glob([
        "hwy/*.h",
        "hwy/ops/*.h",
        "hwy/ops/*-inl.h",
        "hwy/contrib/algo/*.h",
        "hwy/contrib/image/*.h",
        "hwy/contrib/math/*.h",
        "hwy/contrib/random/*.h",
        "hwy/contrib/sort/*.h",
        "hwy/contrib/unroller/*.h",
        "hwy/tests/*.h",
    ]),
    copts = [
        "-DHWY_SHARED_DEFINE",
        "-DHWY_STATIC_DEFINE",
        "-std=c++17",
    ],
    includes = ["."],
    linkopts = select({
        "@platforms//os:windows": [],
        "//conditions:default": ["-lm"],
    }),
    local_defines = [
        "HWY_SHARED_DEFINE",
        "HWY_STATIC_DEFINE",
    ],
)
