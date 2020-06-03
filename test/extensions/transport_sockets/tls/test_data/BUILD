load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

filegroup(
    name = "certs",
    srcs = glob(["*"]),
)

envoy_cc_test_library(
    name = "cert_infos",
    hdrs = glob(["*info.h"]),
)
