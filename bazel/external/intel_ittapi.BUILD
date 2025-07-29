load("@rules_cc//cc:defs.bzl", "cc_library")

licenses(["notice"])

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "lib_ittapi",
    srcs = [
        "include/ittnotify.h",
        "include/jitprofiling.h",
        "src/ittnotify/ittnotify_config.h",
        "src/ittnotify/jitprofiling.c",
    ],
    hdrs = [
        "include/ittnotify.h",
        "src/ittnotify/ittnotify_types.h",
    ],
    includes = ["include/"],
    visibility = ["//visibility:public"],
)
