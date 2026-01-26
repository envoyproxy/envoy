load("@rules_cc//cc:defs.bzl", "cc_library")

licenses(["notice"])  # Apache 2

cc_library(
    name = "fuzzed_data_provider",
    hdrs = ["include/fuzzer/FuzzedDataProvider.h"],
    strip_include_prefix = "include",
    visibility = ["//visibility:public"],
)

libfuzzer_copts = [
    "-fno-sanitize=address,thread,undefined",
    "-fsanitize-coverage=0",
    "-O3",
]

cc_library(
    name = "libfuzzer_main",
    srcs = ["lib/fuzzer/FuzzerMain.cpp"],
    copts = libfuzzer_copts,
    visibility = ["//visibility:public"],
    deps = [":libfuzzer_no_main"],
    alwayslink = True,
)

cc_library(
    name = "libfuzzer_no_main",
    srcs = glob(
        ["lib/fuzzer/Fuzzer*.cpp"],
        exclude = ["lib/fuzzer/FuzzerMain.cpp"],
    ),
    hdrs = glob([
        "lib/fuzzer/Fuzzer*.h",
        "lib/fuzzer/Fuzzer*.def",
    ]),
    copts = libfuzzer_copts,
    visibility = ["//visibility:public"],
    alwayslink = True,
)
