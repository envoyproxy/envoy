load("@rules_cc//cc:defs.bzl", "cc_library")

licenses(["notice"])  # Apache 2

alias(
    name = "libbpf",
    actual = select({
        "//bazel:bzlmod_mode": "@libbpf",
        "//conditions:default": "//bazel/foreign_cc:libbpf_linux",
    }),
    visibility = ["//visibility:public"],
)

alias(
    name = "libevent",
    actual = select({
        "//bazel:bzlmod_mode": "@libevent",
        "//conditions:default": "//bazel/foreign_cc:event",
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

alias(
    name = "liburing",
    actual = select({
        "//bazel:bzlmod_mode": "@liburing",
        "//conditions:default": "//bazel/foreign_cc:liburing_linux",
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

cc_library(
    name = "libcxx_msan_wrapper",
    visibility = ["//visibility:public"],
    deps = select({
        "//bazel:msan_build": ["@msan_libs"],
        "//conditions:default": [],
    }),
    alwayslink = 1,
)

cc_library(
    name = "libcxx_tsan_wrapper",
    visibility = ["//visibility:public"],
    deps = select({
        "//bazel:tsan_build": ["@tsan_libs"],
        "//conditions:default": [],
    }),
    alwayslink = 1,
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

cc_library(
    name = "unicode_icu",
    tags = ["skip_on_windows"],
    # Can not be used for the core dataplane due to security concerns
    visibility = ["//contrib/language/filters/http/source:__pkg__"],
    deps = ["@icu"],
)
