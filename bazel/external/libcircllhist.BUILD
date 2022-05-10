licenses(["notice"])  # Apache 2

cc_library(
    name = "libcircllhist",
    srcs = ["src/circllhist.c"],
    hdrs = [
        "src/circllhist.h",
    ],
    copts = select({
        "@envoy//bazel:windows_x86_64": ["-DWIN32"],
        "//conditions:default": [],
    }),
    includes = ["src"],
    visibility = ["//visibility:public"],
)
