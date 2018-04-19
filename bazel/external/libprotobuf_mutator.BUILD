cc_library(
    name = "libprotobuf_mutator",
    srcs = glob(["src/**/*.cc", "src/**/*.h", "port/protobuf.h"], exclude=["**/*_test.cc"]),
    hdrs = ["src/libfuzzer/libfuzzer_macro.h"],
    includes = ["."],
    include_prefix = "libprotobuf_mutator",
    deps = ["//external:protobuf"],
    visibility = ["//visibility:public"],
)
