package(default_visibility = ["//visibility:public"])

# Create a runnable grcov target
genrule(
    name = "grcov",
    srcs = ["@grcov//:grcov"],
    outs = ["grcov_bin"],
    cmd = "cp $< $@ && chmod +x $@",
    executable = True,
)

# Direct alias to the grcov filegroup
alias(
    name = "grcov_filegroup",
    actual = "@grcov//:grcov_bin",
)
