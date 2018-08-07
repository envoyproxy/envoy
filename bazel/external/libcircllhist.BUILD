cc_library(
    name = "libcircllhist",
    srcs = ["src/circllhist.c"],
    hdrs = [
        "src/circllhist.h",
    ],
    includes = ["src"],
    visibility = ["//visibility:public"],
    copts = select({
            "@envoy//bazel:windows_x86_64": ["-DWIN32"],
            "//conditions:default": [],
           }),
)
