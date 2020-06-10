licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)

envoy_package()

filegroup(
    name = "certs",
    srcs = glob(["*.pem"]),
)

envoy_cc_test_library(
    name = "hashes",
    hdrs = glob(["*hash.h"]),
)
