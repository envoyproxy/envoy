load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "radix_tree_lib",
    srcs = [
        "tree.cpp",
        "node.cpp",
        "iterator.cpp",
        "trie_hybrid.cpp",
    ],
    hdrs = [
        "tree.hpp",
        "node.hpp",
        "iterator.cpp",
        "trie_hybrid.hpp",
    ],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:trie_lookup_table_lib",
        "@com_google_absl//absl/strings",
    ],
) 