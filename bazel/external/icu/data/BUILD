package(
    default_visibility = ["//visibility:public"],
)

licenses(["notice"])  # Apache 2.0

exports_files(["LICENSE"])

# Currently, the ICU data is manually generated using tools/icu/generate_data.sh.
filegroup(
    name = "generated",
    srcs = ["data.c"],
)
