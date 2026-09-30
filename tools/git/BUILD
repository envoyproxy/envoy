load("//tools/git:git_launcher.bzl", "git_launcher", "git_runtime")

licenses(["notice"])  # Apache 2

# Hermetic `git`, for shell callers: `bazel run //tools/git:git -- <args>`
git_launcher(
    name = "git",
    visibility = ["//visibility:public"],
)

# Runtime files for the resolved git toolchain - required by `$(GIT)` consumers
# (`genrule.toolchains` provides the make variable but not the inputs).
git_runtime(
    name = "runtime",
    visibility = ["//visibility:public"],
)
