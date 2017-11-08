load("@subpar//:subpar.bzl", "par_binary")

# gcovr is difficult to run from a CI environment because it has hard
# assumptions about its local working directory, which interact poorly
# with `bazel run`. To make gcovr more mobile, we package it into a
# .par file (a mostly-hermetic "Python binary").
par_binary(
    name = "gcovr",
    srcs = [":renamed_gcovr.py"],
    main = ":renamed_gcovr.py",
    visibility = ["//visibility:public"],
)

# par_binary expects its `srcs` to contain only *.py files, but gcovr is
# distributed as a script with no filename extension. Rename it here.
genrule(
    name = "gcovr_to_exec_py",
    srcs = ["scripts/gcovr"],
    outs = ["renamed_gcovr.py"],
    cmd = "cat $(location scripts/gcovr) > $(location renamed_gcovr.py)",
)
