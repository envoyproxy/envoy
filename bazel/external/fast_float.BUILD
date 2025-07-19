load("@rules_cc//cc:cc_library.bzl", "cc_library")

licenses(["notice"])

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "fast_float",
    hdrs = ["include/fast_float/fast_float.h"],
    includes = ["include"],
    strip_include_prefix = "include",
)

# Create the exact path structure V8 expects
genrule(
    name = "create_third_party_structure",
    srcs = ["include/fast_float/fast_float.h"],
    outs = ["third_party/fast_float/src/fast_float.h"],
    cmd = """
        mkdir -p $(RULEDIR)/third_party/fast_float/src
        cp $(SRCS) $(RULEDIR)/third_party/fast_float/src/
    """,
)

cc_library(
    name = "third_party/fast_float/src/fast_float",
    hdrs = [":create_third_party_structure"],
    includes = ["."],
)
