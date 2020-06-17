load("@rules_cc//cc:defs.bzl", "cc_library")
load(":genrule_cmd.bzl", "genrule_cmd")

licenses(["notice"])  # Apache 2

cc_library(
    name = "crypto",
    srcs = [
        "crypto/libcrypto.a",
    ],
    hdrs = glob(["boringssl/include/openssl/*.h"]),
    defines = ["BORINGSSL_FIPS"],
    includes = ["boringssl/include"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "ssl",
    srcs = [
        "ssl/libssl.a",
    ],
    hdrs = glob(["boringssl/include/openssl/*.h"]),
    includes = ["boringssl/include"],
    visibility = ["//visibility:public"],
    deps = [":crypto"],
)

genrule(
    name = "build",
    srcs = glob(["boringssl/**"]),
    outs = [
        "crypto/libcrypto.a",
        "ssl/libssl.a",
    ],
    cmd = genrule_cmd("@envoy//bazel/external:boringssl_fips.genrule_cmd"),
)
