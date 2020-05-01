licenses(["notice"])  # Apache 2

# Starting with 2.11, the jinja2 distribution does not keep the source files in a directory with
# the expected package name (it uses 'src/jinja2' instead of 'jinja2'), so we provide a genrule to
# copy src/jinja2/*.py to jinja2/*.py.

src_files = glob(["src/jinja2/*.py"])

genrule(
    name = "jinja_files",
    srcs = src_files,
    outs = [f.replace("src/jinja2/", "jinja2/") for f in src_files],
    cmd = "\n".join(
        ["mkdir -p $$(dirname $(location %s)) && cp $(location %s) $(location :%s)" % (
            f,
            f,
            f.replace("src/jinja2/", "jinja2/"),
        ) for f in src_files],
    ),
    visibility = ["//visibility:private"],
)

py_library(
    name = "jinja2",
    srcs = [":jinja_files"],
    visibility = ["//visibility:public"],
    deps = ["@com_github_pallets_markupsafe//:markupsafe"],
)
