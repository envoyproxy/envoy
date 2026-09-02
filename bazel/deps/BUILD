load("@rules_cc//cc:defs.bzl", "cc_library")

licenses(["notice"])  # Apache 2

alias(
    name = "buildifier",
    actual = select({
        "//bazel:bzlmod_mode": "@buildifier",
        "//conditions:default": "@buildtools//buildifier",
    }),
    visibility = ["//visibility:public"],
)

alias(
    name = "buildozer",
    actual = select({
        "//bazel:bzlmod_mode": "@buildifier//:buildozer",
        "//conditions:default": "@buildtools//buildozer",
    }),
    visibility = ["//visibility:public"],
)

alias(
    name = "hyperscan",
    actual = select({
        "//bazel:bzlmod_mode": "@hyperscan",
        "//conditions:default": "//bazel/foreign_cc:hyperscan",
    }),
    visibility = [
        "//contrib/hyperscan/matching/input_matchers/source:__pkg__",
        "//contrib/hyperscan/matching/input_matchers/test:__pkg__",
    ],
)

alias(
    name = "ipp-crypto",
    actual = select({
        "//bazel:bzlmod_mode": "@ipp-crypto//:crypto_mb",
        "//conditions:default": "//bazel/foreign_cc:ipp-crypto",
    }),
    visibility = ["//contrib/cryptomb/private_key_providers/source:__pkg__"],
)

alias(
    name = "jemalloc",
    actual = select({
        "//bazel:bzlmod_mode": "@jemalloc",
        "//conditions:default": "//bazel/foreign_cc:jemalloc",
    }),
    visibility = ["//visibility:public"],
)

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
    name = "libprotobuf-mutator",
    actual = select({
        "//bazel:bzlmod_mode": "@libprotobuf-mutator",
        "//conditions:default": "@libprotobuf-mutator//:libprotobuf_mutator",
    }),
    visibility = ["//visibility:public"],
)

alias(
    name = "librdkafka",
    actual = select({
        "//bazel:bzlmod_mode": "@librdkafka",
        "//conditions:default": "//bazel/foreign_cc:librdkafka",
    }),
    visibility = ["//visibility:public"],
)

cc_library(
    name = "librdkafka_headers",
    hdrs = select({
        "//bazel:bzlmod_mode": ["@librdkafka//:headers"],
        "//conditions:default": [],
    }),
    visibility = ["//visibility:public"],
)

alias(
    name = "libsxg",
    actual = select({
        "//bazel:bzlmod_mode": "@libsxg",
        "//conditions:default": "//bazel/foreign_cc:libsxg",
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

alias(
    name = "nghttp2",
    actual = select({
        "//bazel:bzlmod_mode": "@nghttp2",
        "//conditions:default": "//bazel/foreign_cc:nghttp2",
    }),
    visibility = ["//visibility:public"],
)

alias(
    name = "qatlib",
    actual = select({
        "//bazel:bzlmod_mode": "@qatlib//:qat",
        "//conditions:default": "//bazel/foreign_cc:qatlib",
    }),
    visibility = ["//visibility:public"],
)

alias(
    name = "qatzip",
    actual = select({
        "//bazel:bzlmod_mode": "@qatzip",
        "//conditions:default": "//bazel/foreign_cc:qatzip",
    }),
    visibility = ["//visibility:public"],
)

alias(
    name = "qat-zstd",
    actual = select({
        "//bazel:bzlmod_mode": "@qat-zstd",
        "//conditions:default": "//bazel/foreign_cc:qat-zstd",
    }),
    visibility = ["//visibility:public"],
)

alias(
    name = "uadk",
    actual = select({
        "//bazel:bzlmod_mode": "@uadk",
        "//conditions:default": "//contrib/kae:uadklib",
    }),
    visibility = ["//visibility:public"],
)

alias(
    name = "vectorscan",
    actual = select({
        "//bazel:bzlmod_mode": "@vectorscan",
        "//conditions:default": "//bazel/foreign_cc:vectorscan",
    }),
    visibility = ["//visibility:public"],
)

alias(
    name = "vpp_vcl",
    actual = select({
        "//bazel:bzlmod_mode": "@vpp-vcl",
        "//conditions:default": "//bazel/foreign_cc:vpp_vcl",
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
    deps = select({
        "//bazel:bzlmod_mode": ["@icu"],
        "//conditions:default": ["//bazel/foreign_cc:unicode_icu_build"],
    }),
)
