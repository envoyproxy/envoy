load(
    "//clang_tools/support:clang_tools.bzl",
    "clang_tools_cc_binary",
    "clang_tools_cc_library",
    "clang_tools_cc_test",
)

licenses(["notice"])  # Apache 2

clang_tools_cc_binary(
    name = "api_booster",
    srcs = ["main.cc"],
    deps = [
        ":proto_cxx_utils_lib",
        "@clang_tools//:clang_astmatchers",
        "@clang_tools//:clang_basic",
        "@clang_tools//:clang_tooling",
        "@envoy//tools/type_whisperer:api_type_db_lib",
    ],
)

clang_tools_cc_library(
    name = "proto_cxx_utils_lib",
    srcs = ["proto_cxx_utils.cc"],
    hdrs = ["proto_cxx_utils.h"],
    deps = [
        "@com_google_absl//absl/container:node_hash_map",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/types:optional",
    ],
)

clang_tools_cc_test(
    name = "proto_cxx_utils_test",
    srcs = ["proto_cxx_utils_test.cc"],
    deps = [":proto_cxx_utils_lib"],
)
