load("@rules_cc//cc:defs.bzl", "cc_library")
load("//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

cc_library(
    name = "http_parser__permissive",
    srcs = [
        "http_parser_permissive.cc",
        "http_parser_permissive.h",
    ],
    hdrs = ["http_parser_permissive.h"],
    # This compiler flag is set to an arbtitrarily high number so
    # as to effectively disables the http_parser header limit, as
    # we do our own checks in the conn manager and codec.
    copts = ["-DHTTP_MAX_HEADER_SIZE=0x2000000"],
    includes = ["."],
    visibility = ["//visibility:public"],
)
