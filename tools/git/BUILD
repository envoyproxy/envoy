load("//tools/git:defs.bzl", "git_bin", "git_runtime")

licenses(["notice"])  # Apache 2

# Runtime files for the resolved git toolchain - required by `$(GIT)` consumers
# (`genrule.toolchains` provides the make variable but not the inputs).
git_runtime(
    name = "runtime",
    visibility = ["//visibility:public"],
)

# The hermetic git executable alone, which CI resolves to an absolute path as `GIT_BIN`
# (see `hermetic_git` in `ci/bazel_fun.sh`). Its runtime files are siblings in the toolchain repo.
git_bin(
    name = "bin",
    visibility = ["//visibility:public"],
)
