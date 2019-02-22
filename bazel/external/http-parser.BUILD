licenses(["notice"])  # Apache 2

cc_library(
    name = "http_parser",
    srcs = [
        "http_parser.c",
        "http_parser.h",
    ],
    hdrs = ["http_parser.h"],
    copts = ["-DHTTP_MAX_HEADER_SIZE=0x7fffffff"],
    includes = ["."],
    visibility = ["//visibility:public"],
)
