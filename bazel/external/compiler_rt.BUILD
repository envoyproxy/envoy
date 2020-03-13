licenses(["notice"])  # Apache 2

cc_library(
    name = "fuzzed_data_provider",
    hdrs = ["utils/FuzzedDataProvider.h"],
    # This is moving from lib/fuzzer/utils to include/fuzzer after LLVM 9.0.
    include_prefix = "compiler_rt/fuzzer",
    visibility = ["//visibility:public"],
)
