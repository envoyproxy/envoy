load("@rules_cc//cc:cc_library.bzl", "cc_library")

licenses(["notice"])  # Apache 2

cc_library(
    name = "fmt",
    srcs = [
        #"src/fmt.cc", # No C++ module support, yet in Bazel (https://github.com/bazelbuild/bazel/pull/19940)
        "src/format.cc",
        "src/os.cc",
    ],
    hdrs = glob([
        "include/fmt/*.h",
    ]),
    copts = select({
        "@rules_cc//cc/compiler:msvc-cl": ["-utf-8"],
        "//conditions:default": [],
    }),
    includes = ["include"],
    strip_include_prefix = "include",  # workaround: only needed on some macOS systems (see https://github.com/bazelbuild/bazel-central-registry/issues/1537)
    visibility = ["//visibility:public"],
)
