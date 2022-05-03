licenses(["notice"])  # Apache 2

cc_library(
    name = "http_parser",
    srcs = [
        "http_parser.c",
        "http_parser.h",
    ],
    hdrs = ["http_parser.h"],
    # This compiler flag is set to an arbtitrarily high number so
    # as to effectively disables the http_parser header limit, as
    # we do our own checks in the conn manager and codec.
    copts = ["-DHTTP_MAX_HEADER_SIZE=0x2000000"] + select({
        "@envoy//bazel:uhv_enabled": ["-DHTTP_PARSER_STRICT=0"],
        "//conditions:default": [],
    }),
    includes = ["."],
    visibility = ["//visibility:public"],
)
