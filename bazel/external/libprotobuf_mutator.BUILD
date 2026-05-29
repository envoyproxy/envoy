load("@rules_cc//cc:defs.bzl", "cc_library")

licenses(["notice"])  # Apache 2

cc_library(
    name = "libprotobuf_mutator",
    srcs = glob(
        [
            "src/*.cc",
            "src/libfuzzer/*.cc",
        ],
        exclude = [
            "src/*_test.cc",
            "src/libfuzzer/*_test.cc",
        ],
    ),
    hdrs = glob([
        "src/*.h",
        "port/*.h",
        "src/libfuzzer/*.h",
    ]),
    include_prefix = "libprotobuf_mutator",
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "@com_google_protobuf//:protobuf",
    ],
)
