cc_library(
    name = "libcircllhist",
    srcs = ["src/circllhist.c"],
    hdrs = [
        "src/circllhist.h",
        "src/circllhist_config.h",  # Generated.
    ],
    includes = ["src"],
    visibility = ["//visibility:public"],
)

genrule(
    name = "circllhist_config",
    srcs = ["src/circllhist_config.h.in"],
    outs = ["src/circllhist_config.h"],
    cmd = "cp $< $@",
)
