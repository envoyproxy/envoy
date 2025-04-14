licenses(["notice"])  # Apache 2

cc_library(
    name = "crypto",
    srcs = [
        "crypto/libcrypto.a",
    ],
    hdrs = glob(["include/openssl/*.h"]),
    defines = ["BORINGSSL_FIPS"],
    includes = ["include"],
    visibility = ["//visibility:public"],
)

cc_library(
    name = "ssl",
    srcs = [
        "ssl/libssl.a",
    ],
    hdrs = glob(["include/openssl/*.h"]),
    includes = ["include"],
    visibility = ["//visibility:public"],
    deps = [":crypto"],
)

genrule(
    name = "build",
    srcs = glob(["**"]),
    outs = [
        "crypto/libcrypto.a",
        "ssl/libssl.a",
    ],
    cmd = "$(location {}) $(location crypto/libcrypto.a) $(location ssl/libssl.a)".format("@envoy//bazel/external:aws_lc.genrule_cmd"),
    tools = ["@envoy//bazel/external:aws_lc.genrule_cmd"],
)
