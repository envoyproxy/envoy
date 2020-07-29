package(
    default_visibility = ["//visibility:public"],
)

licenses(["notice"])  # Apache 2.0

exports_files(["LICENSE"])

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
