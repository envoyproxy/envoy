package(
    default_visibility = ["//visibility:public"],
)

licenses(["notice"])  # Apache 2.0

exports_files(["LICENSE"])

# The ICU data is based on https://github.com/nodejs/node "icu-small", filtered using:
# https://raw.githubusercontent.com/nodejs/node/634a9a97f4b380390352543452aed6c7c9defcb4/tools/icu/icu_small.json.
#
# Currently, the ICU data is manually generated using tools/icu/generate_data.sh.
filegroup(
    name = "data_files",
    srcs = glob(["data.c.gz.*"]),
)

# Data files are compressed and splitted.
genrule(
    name = "merge_data",
    srcs = [":data_files"],
    outs = ["data.c"],
    cmd = "cat $(locations :data_files) | gunzip > $@",
)
