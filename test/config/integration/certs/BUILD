load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

filegroup(
    name = "certs",
    srcs = glob([
        "*.pem",
        "*.der",
    ]),
)

envoy_cc_test_library(
    name = "hashes",
    hdrs = glob(["*hash.h"]),
)

envoy_cc_test_library(
    name = "certs_info",
    hdrs = glob(["*info.h"]),
)
