load("@rules_cc//cc:defs.bzl", "cc_library")

licenses(["notice"])  # Apache 2

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "simdutf",
    srcs = ["simdutf.cpp"],
    hdrs = [
        "simdutf.h",
        # TODO(jwendell): Remove once LLVM toolchain is bumped to a version
        # whose libc++ provides std::atomic_ref (LLVM 19+).
        "atomic_ref_polyfill.h",
    ],
)
