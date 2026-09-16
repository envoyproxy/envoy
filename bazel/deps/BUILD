load("@rules_cc//cc:defs.bzl", "cc_library")

licenses(["notice"])  # Apache 2

alias(
    name = "buildifier",
    actual = "@buildifier",
    visibility = ["//visibility:public"],
)

alias(
    name = "buildozer",
    actual = "@buildifier//:buildozer",
    visibility = ["//visibility:public"],
)

alias(
    name = "hyperscan",
    actual = "@hyperscan",
    visibility = [
        "//contrib/hyperscan/matching/input_matchers/source:__pkg__",
        "//contrib/hyperscan/matching/input_matchers/test:__pkg__",
    ],
)

alias(
    name = "ipp-crypto",
    actual = "@ipp-crypto//:crypto_mb",
    visibility = ["//contrib/cryptomb/private_key_providers/source:__pkg__"],
)

alias(
    name = "jemalloc",
    actual = "@jemalloc",
    visibility = ["//visibility:public"],
)

alias(
    name = "libbpf",
    actual = "@libbpf",
    visibility = ["//visibility:public"],
)

alias(
    name = "libevent",
    actual = "@libevent",
    visibility = ["//visibility:public"],
)

alias(
    name = "libmaxminddb",
    actual = "@libmaxminddb",
    visibility = ["//visibility:public"],
)

alias(
    name = "libprotobuf-mutator",
    actual = "@libprotobuf-mutator",
    visibility = ["//visibility:public"],
)

alias(
    name = "librdkafka",
    actual = "@librdkafka",
    visibility = ["//visibility:public"],
)

cc_library(
    name = "librdkafka_headers",
    hdrs = ["@librdkafka//:headers"],
    visibility = ["//visibility:public"],
)

alias(
    name = "libsxg",
    actual = "@libsxg",
    visibility = ["//visibility:public"],
)

alias(
    name = "liburing",
    actual = "@liburing",
    visibility = ["//visibility:public"],
)

alias(
    name = "luajit",
    actual = "@luajit",
    visibility = ["//visibility:public"],
)

alias(
    name = "nghttp2",
    actual = "@nghttp2",
    visibility = ["//visibility:public"],
)

alias(
    name = "qatlib",
    actual = "@qatlib//:qat",
    visibility = ["//visibility:public"],
)

alias(
    name = "qatzip",
    actual = "@qatzip",
    visibility = ["//visibility:public"],
)

alias(
    name = "qat-zstd",
    actual = "@qat-zstd",
    visibility = ["//visibility:public"],
)

alias(
    name = "vectorscan",
    actual = "@vectorscan",
    visibility = ["//visibility:public"],
)

alias(
    name = "vpp_vcl",
    actual = "@vpp-vcl",
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
