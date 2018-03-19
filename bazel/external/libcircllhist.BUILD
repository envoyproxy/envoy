cc_library(
    name = "libcircllhist",
    hdrs = glob([
        'src/*.h',
        'src/*.c',
    ]),
    includes = ["src"],
    visibility = ["//visibility:public"],
)
genrule(
    name = 'circllhist_config_gen',
    srcs = ['src/circllhist_config.h.in'],
    outs = ['src/circllhist_config.h'],
    cmd = 'cp $< $@',
)