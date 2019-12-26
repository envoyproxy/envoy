licenses(["notice"])  # Apache 2

# The apache-thrift distribution does not keep the thrift files in a directory with the
# expected package name (it uses src/Thrift.py vs src/thrift/Thrift.py), so we provide a
# genrule to copy src/**/*.py to thrift/**/*.py.
src_files = glob(["src/**/*.py"])

genrule(
    name = "thrift_files",
    srcs = src_files,
    outs = [f.replace("src/", "thrift/") for f in src_files],
    cmd = "\n".join(
        ["mkdir -p $$(dirname $(location %s)) && cp $(location %s) $(location :%s)" % (
            f,
            f,
            f.replace("src/", "thrift/"),
        ) for f in src_files],
    ),
    visibility = ["//visibility:private"],
)

py_library(
    name = "apache_thrift",
    srcs = [":thrift_files"],
    visibility = ["//visibility:public"],
    deps = ["@six"],
)
