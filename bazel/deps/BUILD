load("@rules_cc//cc:defs.bzl", "cc_library")

licenses(["notice"])  # Apache 2

alias(
    name = "libevent",
    actual = select({
        "//bazel:bzlmod_mode": "@libevent",
        "//conditions:default": "//bazel/foreign_cc:event",
    }),
    visibility = ["//visibility:public"],
)

alias(
    name = "luajit",
    actual = select({
        "//bazel:bzlmod_mode": "@luajit",
        "//conditions:default": "//bazel/foreign_cc:luajit",
    }),
    visibility = ["//visibility:public"],
)

alias(
    name = "libmaxminddb",
    actual = select({
        "//bazel:bzlmod_mode": "@libmaxminddb",
        "//conditions:default": "//bazel/foreign_cc:maxmind",
    }),
    visibility = ["//visibility:public"],
)

cc_library(
    name = "maxmind_linux_darwin",
    srcs = [],
    visibility = ["//visibility:public"],
    deps = select({
        "//bazel:linux": [":libmaxminddb"],
        "//bazel:darwin_any": [":libmaxminddb"],
        "//conditions:default": [],
    }),
)
